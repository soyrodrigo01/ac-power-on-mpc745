# Auto Power-On After Outage on the Redragon MPC745 Mini PC

**Making an AMD Ryzen mini PC boot itself when the power comes back — when the BIOS won't let you.**

---

## The problem

The Redragon MPC745 (board `RED-MPC745-1610BK`, AMI BIOS `MC3LY.03`, AMD Ryzen 7 8845HS "Hawk Point") has no visible option to make the machine power itself on after an AC outage. The setting exists in the firmware — AMD CBS exposes it as **`Ac Loss Control`** with values `Always Off / Always On / Reserved / Previous / Auto` — but on this build it is **suppressed from the Setup UI** and every programmatic write to it is rejected.

If you run a home server / lab on one of these and you want it to survive a power blip without someone pressing the button, this is a real problem.

## Why it's hard

The setting lives in a UEFI variable called `AmdSetupPHX` (GUID `3a997502-647a-4c82-998e-52ef9486a247`), at payload offset `0x19D`, one byte:

| value | meaning |
|------:|---------|
| `0x00` | Always Off |
| `0x01` | Always On |
| `0x02` | Reserved |
| `0x03` | Previous |
| `0x0F` | Auto |

That variable is **write-protected at the SMM (System Management Mode) layer**. Every normal path fails with `EFI_WRITE_PROTECTED (0x8000000000000008)`:

- the BIOS Setup UI save (it appears to save, then reverts after reboot)
- AMI's own SCEWIN / AMISCE tool (`/cpwd`, `/cpwds`, `/cpwde` all return *"Admin password does not exist"*)
- `HiiConfigRouting.RouteConfig`
- plain `RuntimeServices.SetVariable`
- vendor unlock mailboxes and DXE-side policy-table patches

The enforcement is inside `NvramSmm`, an SMM twin of `NvramDxe`. Once SMM takes its lock, external writes cannot release it. Patching DXE-side RAM policy structures has no effect on the refusal.

## The solution

Bypass the NVRAM entirely and program the **hardware register** the setting actually controls.

The exact MC3LY firmware (`FchTacomaDxe`, `FchTacomaSmmInit`) contains this sequence:

```c
/* AMD AGESA — public source, e.g. Proc/Fch/Common/FchLib.c */
VOID AcLossControl(IN UINT8 AcLossControlValue) {
    AcLossControlValue &= 0x03;
    AcLossControlValue |= BIT2;
    RwMem(ACPI_MMIO_BASE + PMIO_BASE + FCH_PMIOA_REG5B,
          AccessWidth8, 0xF0, AcLossControlValue);
}
```

Which is, at the instruction level:

```
movzx r9d, byte ptr [config + 0x51]   ; the Ac Loss Control value
and   r9d, 0x3
or    r9d, 0x4
mov   ecx, 0xFED8035B                 ; ACPI_MMIO_BASE(0xFED80000)+PMIO_BASE(0x300)+0x5B
mov   r8d, 0xF0                       ; preserve upper nibble
call  RMW8                            ; read → (v&0xF0)|0x05 → write → readback
```

So the register is **PMIO `0x5B` at MMIO `0xFED8035B`**, byte access, read-modify-write with mask `0xF0`. The lower nibble is the policy, the upper nibble is a read-only mirror of it.

AMD's public *Bolton FCH BIOS Developer's Guide* §3.6 documents the same register:

> The manner in which the system will restart following a power-fail / power-restore cycle depends on the setting of PMIO register 5Bh[1:0]:
> `01b` — the system will always restart after the power is restored.
> Bits[7:4] are read-only and reflect the same values as bits[3:0].

Mapping the setup values through `(cfg & 3) | 4`:

| setup value | nibble written | byte readback | meaning |
|------------:|---------------:|--------------:|---------|
| Always Off (`0`) | `0x4` | `0x44` | stays off |
| **Always On (`1`)** | **`0x5`** | **`0x55`** | **auto-starts** |
| Reserved (`2`) | `0x6` | `0x66` | reserved |
| Previous/Auto (`3`) | `0x7` | `0x77` | previous state |

A live read on the affected unit returned `0x44` (Always Off), matching the firmware and the observed failure to auto-start.

## The fix

Write `0x55` to `0xFED8035B` after every boot. The BIOS re-arms `0x44` at each power-on (from the protected NVRAM value), so a small root service re-arms it right back.

### One-time verification (read-only)

```c
/* open /dev/mem O_RDONLY, mmap one page at 0xFED80000, read byte at offset 0x35B */
```

A minimal read-only helper returned:

```
PMIO[0x5B] @ 0xFED8035B = 0x44
lower_nibble=0x4 policy_bits=0 mirror_nibble=0x4
```

### The writer (fail-closed)

The writer refuses to touch anything unless **all** of these hold:

- exact board/BIOS DMI match (`RED-MPC745-1610BK` + `MC3LY.03`)
- current byte is exactly `0x44` (already-on `0x55` is a no-op; anything else aborts)
- the three neighbouring PMIO bytes are unchanged afterwards

It performs **exactly one 8-bit MMIO store** — the firmware-equivalent RMW `(before & 0xF0) | 0x05` = `0x45` — then requires a stable readback of `0x55`. No retry on any failure path. It never touches NVRAM, SMM, SPI flash, boot order, or anything else.

Result on real hardware:

```
pmio_ac_loss addr=0xFED8035B before=0x44 write=0x45 after1=0x55 after2=0x55 result=OK_WROTE
neighbour_dword before=4415FF01 after=5515FF01
```

### The service

A hardened `systemd` oneshot runs the writer at every boot:

```ini
[Unit]
Description=Arm automatic power-on after AC loss
After=local-fs.target
Before=multi-user.target
ConditionPathExists=/dev/mem

[Service]
Type=oneshot
ExecStart=/usr/local/sbin/redragon-mpc745-ac-loss-on
RemainAfterExit=yes
Restart=no
User=root
Group=root
CapabilityBoundingSet=CAP_SYS_RAWIO
NoNewPrivileges=yes
PrivateDevices=no
DevicePolicy=closed
DeviceAllow=/dev/mem rw
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
ProtectKernelTunables=yes
ProtectKernelModules=yes
ProtectControlGroups=yes
RestrictAddressFamilies=AF_UNIX
RestrictNamespaces=yes
MemoryDenyWriteExecute=yes
SystemCallFilter=@system-service

[Install]
WantedBy=multi-user.target
```

Install, then verify:

```sh
systemctl enable --now redragon-mpc745-ac-loss-on.service
systemctl status redragon-mpc745-ac-loss-on.service
journalctl -u redragon-mpc745-ac-loss-on.service
```

## Validation

1. **Live register:** `0x55` after the service runs; `0x44` again after a fresh boot before the service runs.
2. **Neighbour bytes:** unchanged (`4415FF01 → 5515FF01` — only the target nibble moved).
3. **NVRAM:** `AmdSetupPHX` SHA-256 and byte `0x19D` unchanged (`0x00`) — nothing persistent was modified.
4. **Real AC-loss test:** clean shutdown → unplug AC 30 s → reconnect → **the machine started itself** without touching the power button. This is the only test that proves it works; a normal reboot does not, because the register is re-armed at every boot.

## Important caveats

- **This is volatile.** The write survives only until the next power cycle; the service re-arms it after each boot. If the service is disabled, behaviour reverts to *Always Off*.
- **Hardware-specific.** Tested only on `RED-MPC745-1610BK` / BIOS `MC3LY.03` / Ryzen 7 8845HS. The writer refuses to run on any other DMI/BIOS combination. Other AMI/AMD boards use the same `AcLossControl` mechanism (`PMIO 5Bh`), but the exact register and encoding must be re-derived per firmware — **do not blindly copy the address**.
- **A working RTC/coin cell matters.** If the RTC doesn't survive a power outage, don't assume this register does. Check that the clock keeps time across a real outage before trusting the feature.
- **Not a BIOS unlock.** This does not change firmware settings, unlock hidden menus, or persist in NVRAM. It programs a documented AMD FCH power-management register at runtime — the same register the BIOS programs at boot.
- **Recovery:** a plain reboot (or `systemctl disable --now redragon-mpc745-ac-loss-on.service`) restores the default behaviour. No flash, no NVRAM, no brick risk.

## Why not just flash a custom BIOS?

No vendor BIOS update for the MPC745 is published (Redragon's download page is empty for this model), and the unit is an ODM rebadge (FCC ID `2A5EQ-2504MP1`, shared with the LTC MC741) with no public firmware image and no documented recovery path. Cross-flashing is unsafe and unnecessary here — the runtime PMIO write achieves the goal with zero persistent modification.

## Files

| file | purpose |
|------|---------|
| `pmio_write.c` + `pmio_policy.c` + `pmio_map.c` + `pmio_identity.c` | the fail-closed writer |
| `pmio_read.c` | read-only verifier |
| `redragon-mpc745-ac-loss-on.service` | systemd oneshot |
| `tests/` | policy / mapping / identity unit tests |
| `FINDINGS.md` | full reverse-engineering notes and evidence |

Build and test:

```sh
make test all
sudo systemctl enable --now redragon-mpc745-ac-loss-on.service
```

## Summary

The BIOS setting is unreachable and write-protected at the SMM layer, so no amount of tooling can persist it. But the *hardware register* it controls — AMD FCH PMIO `0x5B` at `0xFED8035B` — is a plain byte with a documented layout. A root one-shot that writes `0x55` after each boot is all it takes.

The machine now powers itself on when the lights come back.

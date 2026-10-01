CC ?= gcc
CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -fanalyzer

.PHONY: all test clean

all: pmio_read pmio_write

pmio_read: pmio_read.c
	$(CC) $(CFLAGS) $< -o $@

pmio_write: pmio_write.c pmio_policy.c pmio_map.c pmio_identity.c pmio_policy.h pmio_map.h pmio_identity.h
	$(CC) $(CFLAGS) pmio_write.c pmio_policy.c pmio_map.c pmio_identity.c -o $@

test:
	$(CC) $(CFLAGS) -I. tests/test_pmio_policy.c pmio_policy.c -o /tmp/test_pmio_policy
	/tmp/test_pmio_policy
	$(CC) $(CFLAGS) -I. tests/test_pmio_map.c pmio_map.c -o /tmp/test_pmio_map
	/tmp/test_pmio_map
	$(CC) $(CFLAGS) -I. tests/test_pmio_identity.c pmio_identity.c -o /tmp/test_pmio_identity
	/tmp/test_pmio_identity

clean:
	rm -f pmio_read pmio_write /tmp/test_pmio_policy /tmp/test_pmio_map /tmp/test_pmio_identity

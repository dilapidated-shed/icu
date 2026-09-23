IDRIC ?= idris2
CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic
OPENSSL_LIBS ?= -lssl -lcrypto

.PHONY: all clean check-native

all: libicu_transport.so
	$(IDRIC) --build icu.ipkg

libicu_transport.so: native/transport.c
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $< $(OPENSSL_LIBS)

check-native: native/transport.c tests/RedirectPolicyTests.c
	$(CC) $(CFLAGS) -Werror -fPIC -shared -o /tmp/icu-transport-check.so native/transport.c $(OPENSSL_LIBS)
	$(CC) $(CFLAGS) -Werror -o /tmp/icu-redirect-policy tests/RedirectPolicyTests.c $(OPENSSL_LIBS)
	/tmp/icu-redirect-policy
	rm -f /tmp/icu-transport-check.so /tmp/icu-redirect-policy

clean:
	rm -rf build libicu_transport.so

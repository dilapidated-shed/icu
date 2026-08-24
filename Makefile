IDRIC ?= idris2
CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic
OPENSSL_LIBS ?= -lssl -lcrypto

.PHONY: all clean check-native

all: libicu_transport.so
	$(IDRIC) --build icu.ipkg

libicu_transport.so: native/transport.c
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $< $(OPENSSL_LIBS)

check-native: native/transport.c
	$(CC) $(CFLAGS) -Werror -fPIC -shared -o /tmp/icu-transport-check.so $< $(OPENSSL_LIBS)
	rm -f /tmp/icu-transport-check.so

clean:
	rm -rf build libicu_transport.so

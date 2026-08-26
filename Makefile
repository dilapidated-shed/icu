IDRIC ?= idris2
CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic
OPENSSL_LIBS ?= -lssl -lcrypto
IDRIC_SOURCES := $(wildcard src/*.idric tests/*.idric)

.PHONY: all clean check-native check-idric-vocabulary

all: check-idric-vocabulary libicu_transport.so
	$(IDRIC) --build icu.ipkg

check-idric-vocabulary:
	@if grep -nE '(^|[^[:alnum:]_])Nat([^[:alnum:]_]|$$)' $(IDRIC_SOURCES) README.md; then \
		echo 'error: active Idriç source must use ℕ for natural numbers' >&2; \
		exit 1; \
	fi

libicu_transport.so: native/transport.c
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $< $(OPENSSL_LIBS)

check-native: native/transport.c
	$(CC) $(CFLAGS) -Werror -fPIC -shared -o /tmp/icu-transport-check.so $< $(OPENSSL_LIBS)
	rm -f /tmp/icu-transport-check.so

clean:
	rm -rf build libicu_transport.so

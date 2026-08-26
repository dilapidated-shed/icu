IDRIC ?= idris2
CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic
OPENSSL_LIBS ?= -lssl -lcrypto

.PHONY: all clean check-native check-transport-model

all: libicu_transport.so
	$(IDRIC) --build icu.ipkg

libicu_transport.so: native/transport.c
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $< $(OPENSSL_LIBS)

check-native: native/transport.c
	$(CC) $(CFLAGS) -Werror -fPIC -shared -o /tmp/icu-transport-check.so $< $(OPENSSL_LIBS)
	rm -f /tmp/icu-transport-check.so

check-transport-model: tests/transport_model_oracle.c tests/TransportModelOracle.idric \
		tests/transport-fixtures.json tests/check_transport_equivalence.py
	mkdir -p build/tests
	$(CC) $(CFLAGS) -Werror -o build/tests/transport-model-c \
		tests/transport_model_oracle.c $(OPENSSL_LIBS)
	$(IDRIC) tests/TransportModelOracle.idric \
		-o transport-model-idric
	python3 tests/check_transport_equivalence.py \
		build/tests/transport-model-c build/exec/transport-model-idric

clean:
	rm -rf build libicu_transport.so

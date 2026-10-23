IDRIC ?= idris2
ICK ?=
CC = $(ICK)
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic
ICK_LINK_FLAGS ?= -fno-link-libatomic
OPENSSL_LIBS ?= -lssl -lcrypto

.PHONY: all clean check-native require-ick

require-ick:
	test -n "$(ICK)"
	test -x "$(ICK)"

all: libicu_transport.so
	$(IDRIC) --build icu.ipkg

libicu_transport.so: native/transport.c require-ick
	$(CC) $(ICK_LINK_FLAGS) $(CFLAGS) -fPIC -shared -o $@ $< $(OPENSSL_LIBS)

check-native: require-ick native/transport.c tests/RedirectPolicyTests.c
	$(CC) $(ICK_LINK_FLAGS) $(CFLAGS) -Werror -fPIC -shared -o /tmp/icu-transport-check.so native/transport.c $(OPENSSL_LIBS)
	$(CC) $(ICK_LINK_FLAGS) $(CFLAGS) -Werror -o /tmp/icu-redirect-policy tests/RedirectPolicyTests.c $(OPENSSL_LIBS)
	/tmp/icu-redirect-policy
	$(CC) $(ICK_LINK_FLAGS) $(CFLAGS) -Werror -o /tmp/icu-framing-tests tests/FramingTests.c $(OPENSSL_LIBS)
	/tmp/icu-framing-tests
	rm -f /tmp/icu-transport-check.so /tmp/icu-redirect-policy /tmp/icu-framing-tests

clean:
	rm -rf build libicu_transport.so

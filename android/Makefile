# Fixed NDK build-tool interface; independent of DEX generation.
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
ANDROID_ABI ?= armeabi-v7a
ANDROID_API ?= 24
OPENSSL_REF := f4dc4d58b48d346a8270183f89acf826d459b0ca
TARGET_armeabi-v7a := armv7a-linux-androideabi
TARGET_x86_64 := x86_64-linux-android
OPENSSL_TARGET_armeabi-v7a := android-arm
OPENSSL_TARGET_x86_64 := android-x86_64
EXTRA_armeabi-v7a := -latomic
WORK := $(ROOT)/build/android/$(ANDROID_ABI)
OUTPUT ?= $(WORK)/libicu_transport.so
NDK_BIN := $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/linux-x86_64/bin
COMPILER := $(NDK_BIN)/$(TARGET_$(ANDROID_ABI))$(ANDROID_API)-clang
READELF := $(NDK_BIN)/llvm-readelf

.PHONY: all preflight symbols receipt tests
all: preflight $(OUTPUT) symbols receipt
preflight:
	test -n "$(TARGET_$(ANDROID_ABI))"
	test -x "$(COMPILER)"
	test "$$(git -C "$(OPENSSL_SOURCE)" rev-parse HEAD)" = "$(OPENSSL_REF)"
	test -z "$$(git -C "$(OPENSSL_SOURCE)" status --porcelain --untracked-files=no)"

$(WORK)/openssl/configdata.pm: | preflight
	mkdir -p "$(WORK)"
	test ! -e "$(WORK)/openssl"
	cp -a "$(OPENSSL_SOURCE)" "$(WORK)/openssl"
	cd "$(WORK)/openssl" && env ANDROID_NDK_ROOT="$(ANDROID_NDK_HOME)" PATH="$(NDK_BIN):$$PATH" ./Configure "$(OPENSSL_TARGET_$(ANDROID_ABI))" -D__ANDROID_API__=$(ANDROID_API) no-shared no-pinshared no-tests no-apps no-module

$(WORK)/openssl/libssl.a: $(WORK)/openssl/configdata.pm
	$(MAKE) -C "$(WORK)/openssl" -j2 build_libs > "$(WORK)/openssl-build.log" 2>&1

$(OUTPUT): $(WORK)/openssl/libssl.a $(ROOT)/native/transport.c $(ROOT)/android/jni_transport.c | preflight
	mkdir -p "$(dir $(OUTPUT))"
	"$(COMPILER)" -std=c11 -O2 -fPIC -Wall -Wextra -Wpedantic -Werror -I"$(WORK)/openssl/include" -shared -Wl,--no-undefined -Wl,--exclude-libs,ALL -Wl,-soname,libicu_transport.so "$(ROOT)/native/transport.c" "$(ROOT)/android/jni_transport.c" "$(WORK)/openssl/libssl.a" "$(WORK)/openssl/libcrypto.a" -ldl -pthread $(EXTRA_$(ANDROID_ABI)) -o "$@"

symbols: $(OUTPUT)
	"$(READELF)" -Ws "$(OUTPUT)" > "$(WORK)/symbols.txt"
	grep -F ' Java_Idric_Generated_captureArguments' "$(WORK)/symbols.txt" > /dev/null
	grep -F ' Java_Idric_Generated_idricArgumentCount' "$(WORK)/symbols.txt" > /dev/null
	grep -F ' Java_Idric_Generated_idricArgument' "$(WORK)/symbols.txt" > /dev/null
	grep -F ' Java_Idric_Generated_idricPutString' "$(WORK)/symbols.txt" > /dev/null
	grep -F ' Java_Idric_Generated_idricExit' "$(WORK)/symbols.txt" > /dev/null
	grep -F ' Java_Idric_Generated_icuSendHttp' "$(WORK)/symbols.txt" > /dev/null
	grep -F ' Java_Idric_Generated_icuSendHttps' "$(WORK)/symbols.txt" > /dev/null
	"$(READELF)" -d "$(OUTPUT)" > "$(WORK)/dynamic.txt"
	! grep -E 'NEEDED.*lib(ssl|crypto)\.so' "$(WORK)/dynamic.txt"
	"$(READELF)" -h "$(OUTPUT)" > "$(WORK)/elf-header.txt"

receipt: $(OUTPUT)
	printf 'toolchain\tndk\nabi\t%s\napi\t%s\nopenssl_ref\t%s\n' "$(ANDROID_ABI)" "$(ANDROID_API)" "$(OPENSSL_REF)" > "$(WORK)/native-receipt.tsv"
	printf 'icu_ref\t%s\n' "$$(git -C "$(ROOT)" rev-parse HEAD)" >> "$(WORK)/native-receipt.tsv"
	sed -n 's/^Pkg.Revision = /ndk_revision\t/p' "$(ANDROID_NDK_HOME)/source.properties" >> "$(WORK)/native-receipt.tsv"
	printf 'dex\tNOT_RUN\nphysical_a1\tNOT_RUN\n' >> "$(WORK)/native-receipt.tsv"
	sha256sum "$(OUTPUT)" > "$(WORK)/native.sha256"

tests: preflight $(WORK)/openssl/libssl.a
	"$(COMPILER)" -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -I"$(WORK)/openssl/include" "$(ROOT)/tests/FramingTests.c" "$(WORK)/openssl/libssl.a" "$(WORK)/openssl/libcrypto.a" -ldl -pthread $(EXTRA_$(ANDROID_ABI)) -o "$(WORK)/framing-tests"
	"$(COMPILER)" -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror "$(ROOT)/tests/JNIBoundaryTests.c" -o "$(WORK)/jni-tests"

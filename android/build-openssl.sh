#!/bin/sh
set -eu

source_root=${1:?usage: build-openssl.sh OPENSSL_SOURCE OUTPUT_PREFIX}
output_prefix=${2:?usage: build-openssl.sh OPENSSL_SOURCE OUTPUT_PREFIX}
api=${ANDROID_API:-24}
abi=${ANDROID_ABI:-x86_64}
expected_ref=${OPENSSL_REF:-}

case "$abi" in
  x86_64) configure_target=android-x86_64 ;;
  x86) configure_target=android-x86 ;;
  arm64-v8a) configure_target=android-arm64 ;;
  armeabi-v7a) configure_target=android-arm ;;
  *)
    printf 'unsupported Android ABI: %s\n' "$abi" >&2
    exit 1
    ;;
esac

ndk=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}
if [ -z "$ndk" ] || [ ! -d "$ndk" ]; then
  printf '%s\n' 'ANDROID_NDK_HOME/ANDROID_NDK_ROOT is required' >&2
  exit 1
fi
if [ ! -f "$source_root/Configure" ]; then
  printf 'OpenSSL Configure not found under %s\n' "$source_root" >&2
  exit 1
fi

if [ -n "$expected_ref" ]; then
  actual_ref=$(git -C "$source_root" rev-parse HEAD)
  if [ "$actual_ref" != "$expected_ref" ]; then
    printf 'OpenSSL source mismatch: expected %s, found %s\n' \
      "$expected_ref" "$actual_ref" >&2
    exit 1
  fi
fi

ndk_bin="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
export ANDROID_NDK_ROOT="$ndk"
export PATH="$ndk_bin:$PATH"

rm -rf "$output_prefix"
mkdir -p "$output_prefix"

(
  cd "$source_root"
  if [ -f Makefile ]; then
    make clean
  fi
  ./Configure "$configure_target" \
    -D__ANDROID_API__="$api" \
    no-shared no-tests no-apps no-docs \
    --prefix="$output_prefix" \
    --openssldir="$output_prefix/ssl"
  make -j2
  make install_sw
)

test -f "$output_prefix/include/openssl/ssl.h"
test -f "$output_prefix/lib/libssl.a"
test -f "$output_prefix/lib/libcrypto.a"
printf 'ICU OpenSSL ABI\t%s\n' "$abi"
printf 'ICU OpenSSL API\t%s\n' "$api"
if [ -n "$expected_ref" ]; then
  printf 'ICU OpenSSL source\t%s\n' "$expected_ref"
fi

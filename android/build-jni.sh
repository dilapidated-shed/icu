#!/bin/sh
set -eu

repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
output=${1:-"$repo_root/build/android/libicu_transport.so"}
api=${ANDROID_API:-24}
abi=${ANDROID_ABI:-x86_64}
openssl_root=${OPENSSL_ROOT:-}

case "$abi" in
  x86_64) target=x86_64-linux-android ;;
  x86) target=i686-linux-android ;;
  arm64-v8a) target=aarch64-linux-android ;;
  armeabi-v7a) target=armv7a-linux-androideabi ;;
  *)
    printf 'unsupported Android ABI: %s\n' "$abi" >&2
    exit 1
    ;;
esac

ndk=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}
if [ -z "$ndk" ]; then
  android_home=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
  [ -n "$android_home" ] || {
    printf '%s\n' 'ANDROID_HOME/ANDROID_SDK_ROOT is required' >&2
    exit 1
  }
  ndk=$(find "$android_home/ndk" -mindepth 1 -maxdepth 1 -type d 2>/dev/null |
    sort -V | tail -n 1)
fi
[ -n "$ndk" ] && [ -d "$ndk" ] || {
  printf '%s\n' 'Android NDK not found' >&2
  exit 1
}

[ -n "$openssl_root" ] && [ -d "$openssl_root/include" ] || {
  printf '%s\n' 'OPENSSL_ROOT must name the exact Android OpenSSL prefix' >&2
  exit 1
}
[ -f "$openssl_root/lib/libssl.a" ] && [ -f "$openssl_root/lib/libcrypto.a" ] || {
  printf '%s\n' 'OPENSSL_ROOT must contain static libssl.a and libcrypto.a' >&2
  exit 1
}

ndk_bin="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
clang="$ndk_bin/${target}${api}-clang"
readelf="$ndk_bin/llvm-readelf"
[ -x "$clang" ] || {
  printf 'Android %s clang not found: %s\n' "$abi" "$clang" >&2
  exit 1
}
[ -x "$readelf" ] || {
  printf 'Android llvm-readelf not found: %s\n' "$readelf" >&2
  exit 1
}

mkdir -p "$(dirname -- "$output")"
"$clang" \
  -shared -fPIC -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror \
  -I"$ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include" \
  -I"$openssl_root/include" \
  -Wl,--no-undefined -Wl,-z,defs -Wl,-soname,libicu_transport.so \
  "$repo_root/native/transport.c" "$repo_root/native/android_jni.c" \
  "$openssl_root/lib/libssl.a" "$openssl_root/lib/libcrypto.a" \
  -ldl -pthread \
  -o "$output"

symbols=$("$readelf" -Ws "$output")
printf '%s\n' "$symbols" | grep -F ' JNI_OnLoad' >/dev/null
printf '%s\n' "$symbols" | grep -F ' icu_send_http' >/dev/null
printf '%s\n' "$symbols" | grep -F ' icu_send_https' >/dev/null

printf 'ICU Android JNI ABI\t%s\n' "$abi"
printf 'ICU Android API\t%s\n' "$api"
printf 'ICU OpenSSL root\t%s\n' "$openssl_root"

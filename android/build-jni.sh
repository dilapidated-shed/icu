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
  if [ -z "$android_home" ]; then
    printf '%s\n' 'ANDROID_HOME/ANDROID_SDK_ROOT is required' >&2
    exit 1
  fi
  ndk=$(find "$android_home/ndk" -mindepth 1 -maxdepth 1 -type d 2>/dev/null |
    sort -V | tail -n 1)
fi
if [ -z "$ndk" ] || [ ! -d "$ndk" ]; then
  printf '%s\n' 'Android NDK not found' >&2
  exit 1
fi

if [ -z "$openssl_root" ] || [ ! -d "$openssl_root/include" ]; then
  printf '%s\n' 'OPENSSL_ROOT must name the exact Android OpenSSL prefix' >&2
  exit 1
fi
if [ ! -f "$openssl_root/lib/libssl.a" ] ||
   [ ! -f "$openssl_root/lib/libcrypto.a" ]; then
  printf '%s\n' 'OPENSSL_ROOT must contain static libssl.a and libcrypto.a' >&2
  exit 1
fi

ndk_bin="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
clang="$ndk_bin/${target}${api}-clang"
readelf="$ndk_bin/llvm-readelf"
if [ ! -x "$clang" ]; then
  printf 'Android %s clang not found: %s\n' "$abi" "$clang" >&2
  exit 1
fi
if [ ! -x "$readelf" ]; then
  printf 'Android llvm-readelf not found: %s\n' "$readelf" >&2
  exit 1
fi

mkdir -p "$(dirname -- "$output")"
"$clang" \
  -shared -fPIC -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror \
  -I"$openssl_root/include" \
  -Wl,--no-undefined -Wl,-z,defs -Wl,-soname,libicu_transport.so \
  "$repo_root/native/transport.c" "$repo_root/native/android_jni.c" \
  "$openssl_root/lib/libssl.a" "$openssl_root/lib/libcrypto.a" \
  -ldl -pthread \
  -o "$output"

symbols=$("$readelf" -Ws "$output")
for symbol in \
  Java_Idric_Generated_captureArguments \
  Java_Idric_Generated_idricArgumentCount \
  Java_Idric_Generated_idricArgument \
  Java_Idric_Generated_idricPutString \
  Java_Idric_Generated_idricExit \
  Java_Idric_Generated_icuSendHttp \
  Java_Idric_Generated_icuSendHttps \
  icu_send_http \
  icu_send_https
do
  if ! printf '%s\n' "$symbols" | grep -F " $symbol" >/dev/null; then
    printf 'missing Android JNI symbol: %s\n' "$symbol" >&2
    exit 1
  fi
done

printf 'ICU Android JNI ABI\t%s\n' "$abi"
printf 'ICU Android API\t%s\n' "$api"
printf 'ICU OpenSSL root\t%s\n' "$openssl_root"

#!/usr/bin/env bash
set -Eeuo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
output=${1:-"$repo_root/build/android/libicu_transport.so"}
openssl_source=${2:-${OPENSSL_SOURCE:-}}
api=${ANDROID_API:-24}
abi=${ANDROID_ABI:-armeabi-v7a}

case "$abi" in
  armeabi-v7a)
    target=armv7a-linux-androideabi
    openssl_target=android-arm
    extra_link=(-latomic)
    ;;
  x86_64)
    target=x86_64-linux-android
    openssl_target=android-x86_64
    extra_link=()
    ;;
  *)
    echo "unsupported ICU Android ABI: $abi" >&2
    exit 2
    ;;
esac

[[ -n $openssl_source && -d $openssl_source ]] || {
  echo 'OPENSSL_SOURCE or the second argument must name an OpenSSL source checkout' >&2
  exit 2
}

ndk=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}
if [[ -z $ndk ]]; then
  android_home=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
  [[ -n $android_home ]] || {
    echo 'ANDROID_HOME/ANDROID_SDK_ROOT is required when ANDROID_NDK_HOME is unset' >&2
    exit 2
  }
  ndk=$(find "$android_home/ndk" -mindepth 1 -maxdepth 1 -type d 2>/dev/null |
    sort -V | tail -n 1)
fi
[[ -n $ndk && -d $ndk ]] || {
  echo 'Android NDK not found' >&2
  exit 2
}

ndk_bin="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
clang="$ndk_bin/${target}${api}-clang"
readelf="$ndk_bin/llvm-readelf"
[[ -x $clang ]] || {
  echo "Android $abi clang not found: $clang" >&2
  exit 2
}
[[ -x $readelf ]] || {
  echo "Android NDK llvm-readelf not found: $readelf" >&2
  exit 2
}

work=${ICU_ANDROID_BUILD_DIR:-"$repo_root/build/android/$abi"}
openssl_build="$work/openssl"
rm -rf "$openssl_build"
mkdir -p "$work"
cp -a "$openssl_source" "$openssl_build"

export ANDROID_NDK_ROOT="$ndk"
export ANDROID_NDK_HOME="$ndk"
export PATH="$ndk_bin:$PATH"

(
  cd "$openssl_build"
  ./Configure "$openssl_target" \
    -D__ANDROID_API__="$api" \
    no-shared no-pinshared no-tests no-apps no-module
  make -j"${ICU_BUILD_JOBS:-2}" build_libs
)

mkdir -p "$(dirname -- "$output")"
"$clang" -std=c11 -O2 -fPIC -Wall -Wextra -Werror \
  -I"$openssl_build/include" \
  -shared -Wl,--no-undefined -Wl,--exclude-libs,ALL \
  -Wl,-soname,libicu_transport.so \
  "$repo_root/native/transport.c" "$repo_root/android/jni_transport.c" \
  "$openssl_build/libssl.a" "$openssl_build/libcrypto.a" \
  -ldl -pthread "${extra_link[@]}" \
  -o "$output"

symbols=$("$readelf" -Ws "$output")
grep -Fq 'Java_org_isomorphisms_icu_ICU_nativeSendHttp' <<<"$symbols"
grep -Fq 'Java_org_isomorphisms_icu_ICU_nativeSendHttps' <<<"$symbols"
grep -Fq 'icu_send_http' <<<"$symbols"
grep -Fq 'icu_send_https' <<<"$symbols"

dynamic=$("$readelf" -d "$output")
if grep -Eq 'NEEDED.*lib(ssl|crypto)\.so' <<<"$dynamic"; then
  echo 'ICU Android transport unexpectedly depends on dynamic OpenSSL' >&2
  exit 3
fi

machine=$("$readelf" -h "$output" | sed -n 's/^[[:space:]]*Machine:[[:space:]]*//p')
printf 'ICU Android ABI          %s\n' "$abi"
printf 'ICU Android API          %s\n' "$api"
printf 'ICU ELF machine          %s\n' "$machine"
printf 'ICU OpenSSL source       %s\n' "$(git -C "$openssl_source" rev-parse HEAD 2>/dev/null || printf unknown)"

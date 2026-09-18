#!/usr/bin/env bash
set -Eeuo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
dex=${1:-}
native_library=${2:-}
output=${3:-"$repo_root/build/android/icu-phone.tar.gz"}

[[ -n $dex && -f $dex ]] || {
  echo 'usage: android/package-catfood.sh CLASSES.DEX LIBICU_TRANSPORT.SO [OUTPUT.tar.gz]' >&2
  exit 2
}
[[ -n $native_library && -f $native_library ]] || {
  echo 'ICU Cat Food packaging requires the real ARMv7 libicu_transport.so' >&2
  exit 2
}

magic=$(od -An -t x1 -N8 "$dex" | tr -d ' \n')
[[ $magic == 6465780a30333500 ]] || {
  echo 'ICU Cat Food packaging rejected a non-DEX-035 entrypoint' >&2
  exit 3
}

dexdump=${DEXDUMP:-}
if [[ -z $dexdump ]] && command -v dexdump >/dev/null 2>&1; then
  dexdump=$(command -v dexdump)
fi
if [[ -z $dexdump ]]; then
  android_home=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
  if [[ -n $android_home && -d $android_home/build-tools ]]; then
    dexdump=$(find "$android_home/build-tools" -mindepth 2 -maxdepth 2 \
      -type f -name dexdump -perm -u+x 2>/dev/null | sort -V | tail -n 1)
  fi
fi
[[ -n $dexdump && -x $dexdump ]] || {
  echo 'ICU Cat Food packaging requires dexdump to validate classes.dex' >&2
  exit 3
}

dex_dump=$(mktemp)
trap 'rm -f "$dex_dump"' EXIT
if ! "$dexdump" "$dex" >"$dex_dump" 2>&1; then
  cat "$dex_dump" >&2
  echo 'ICU Cat Food packaging rejected an invalid DEX file' >&2
  exit 3
fi
if ! grep -F "Class descriptor  : 'LIdric/Generated;'" "$dex_dump" >/dev/null; then
  echo 'ICU Cat Food packaging requires the direct-DEX Idric.Generated class' >&2
  exit 3
fi

machine=$(readelf -h "$native_library" | sed -n 's/^[[:space:]]*Machine:[[:space:]]*//p')
case "$machine" in
  ARM) ;;
  *)
    echo "ICU Cat Food phone package requires an ARM ELF library; found: ${machine:-unknown}" >&2
    exit 3
    ;;
esac

source_ref=${ICU_SOURCE_REF:-$(git -C "$repo_root" rev-parse HEAD)}
package_ref=${ICU_PACKAGE_REF:-$(git -C "$repo_root" rev-parse HEAD)}
dex_backend_ref=${ICU_DEX_BACKEND_REF:-}
[[ $source_ref =~ ^[0-9a-f]{40}$ ]] || {
  echo 'ICU_SOURCE_REF must be a full 40-hex commit' >&2
  exit 3
}
[[ $package_ref =~ ^[0-9a-f]{40}$ ]] || {
  echo 'ICU_PACKAGE_REF must be a full 40-hex commit' >&2
  exit 3
}
[[ $dex_backend_ref =~ ^[0-9a-f]{40}$ ]] || {
  echo 'ICU_DEX_BACKEND_REF must name the exact full DEX producer commit' >&2
  exit 3
}

output_directory=$(dirname -- "$output")
output_name=$(basename -- "$output")
mkdir -p "$output_directory"
output_directory=$(CDPATH= cd -- "$output_directory" && pwd)
output="$output_directory/$output_name"

stage=$(mktemp -d)
trap 'rm -f "$dex_dump"; rm -rf "$stage"' EXIT

cp "$dex" "$stage/classes.dex"
cp "$native_library" "$stage/libicu_transport.so"

dex_sha=$(sha256sum "$stage/classes.dex" | cut -d' ' -f1)
native_sha=$(sha256sum "$stage/libicu_transport.so" | cut -d' ' -f1)

{
  printf 'target\tphone\n'
  printf 'abi\tarmeabi-v7a\n'
  printf 'source_ref\t%s\n' "$source_ref"
  printf 'package_ref\t%s\n' "$package_ref"
  printf 'dex_backend_ref\t%s\n' "$dex_backend_ref"
  printf 'main_class\tIdric.Generated\n'
  printf 'jni_library\tlibicu_transport.so\n'
  printf 'jni_property\ticu.library\n'
  printf 'dex_sha256\t%s\n' "$dex_sha"
  printf 'jni_sha256\t%s\n' "$native_sha"
  printf 'physical_device_result\tNOT_VERIFIED\n'
} > "$stage/catfood-package.tsv"

(
  cd "$stage"
  sha256sum classes.dex libicu_transport.so catfood-package.tsv > payload.sha256
  tar -czf "$output" classes.dex libicu_transport.so catfood-package.tsv payload.sha256
)

archive_sha=$(sha256sum "$output" | cut -d' ' -f1)
printf 'package\t%s\n' "$output"
printf 'sha256\t%s\n' "$archive_sha"
printf 'source_ref\t%s\n' "$source_ref"
printf 'package_ref\t%s\n' "$package_ref"
printf 'dex_backend_ref\t%s\n' "$dex_backend_ref"
printf 'physical_device_result\tNOT_VERIFIED\n'

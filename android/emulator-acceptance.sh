#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
  printf '%s\n' 'usage: emulator-acceptance.sh CLASSES_DEX LIBICU_TRANSPORT_SO' >&2
  exit 2
fi

classes_dex=$1
transport_library=$2
repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
adb=${ADB:-adb}
port=${ICU_EMULATOR_FIXTURE_PORT:-18080}
remote_dir=/data/local/tmp/icu-dex-jni
work_dir=${TMPDIR:-/tmp}/icu-emulator-acceptance.$$
ready_file=$work_dir/ready
server_log=$work_dir/server.log
server_pid=

cleanup() {
  if [ -n "$server_pid" ]; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  "$adb" reverse --remove "tcp:$port" >/dev/null 2>&1 || true
  rm -rf "$work_dir"
}
trap cleanup EXIT HUP INT TERM

[ -f "$classes_dex" ] || {
  printf 'missing direct DEX input: %s\n' "$classes_dex" >&2
  exit 1
}
[ -f "$transport_library" ] || {
  printf 'missing JNI transport input: %s\n' "$transport_library" >&2
  exit 1
}

mkdir -p "$work_dir"
python3 "$repo_root/android/emulator-http-fixture.py" "$port" "$ready_file" \
  >"$server_log" 2>&1 &
server_pid=$!

attempt=0
while [ ! -f "$ready_file" ]; do
  attempt=$((attempt + 1))
  if [ "$attempt" -ge 100 ]; then
    cat "$server_log" >&2 || true
    printf '%s\n' 'deterministic HTTP fixture did not become ready' >&2
    exit 1
  fi
  sleep 0.1
done

"$adb" wait-for-device
"$adb" shell "rm -rf '$remote_dir' && mkdir -p '$remote_dir'"
"$adb" push "$classes_dex" "$remote_dir/classes.dex" >/dev/null
"$adb" push "$transport_library" "$remote_dir/libicu_transport.so" >/dev/null
"$adb" shell "chmod 755 '$remote_dir/libicu_transport.so'"
"$adb" reverse "tcp:$port" "tcp:$port"

run_icu() {
  command=$1
  "$adb" shell \
    "CLASSPATH='$remote_dir/classes.dex' app_process -Dicu.library='$remote_dir/libicu_transport.so' /system/bin Idric.Generated $command"
}

get_output=$(run_icu "get http://127.0.0.1:$port/get" | tr -d '\r')
if [ "$get_output" != "get-ok" ]; then
  printf 'unexpected ICU emulator GET output: %s\n' "$get_output" >&2
  exit 1
fi

post_output=$(run_icu "post http://127.0.0.1:$port/post hello" | tr -d '\r')
if [ "$post_output" != "post:hello" ]; then
  printf 'unexpected ICU emulator POST output: %s\n' "$post_output" >&2
  exit 1
fi

wait "$server_pid"
server_pid=

printf '%s\n' 'ICU_DIRECT_DEX_EMULATOR 1'
printf '%s\n' 'transport=http-local-fixture'
printf '%s\n' 'get=PASS'
printf '%s\n' 'post=PASS'
printf '%s\n' 'physical_phone=NOT_RUN'
printf '%s\n' 'physical_http_https=NOT_RUN'

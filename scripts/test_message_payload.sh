#!/bin/sh
# Pass a cJSON 1.7.19 source directory (same version as the ESP32 SDK).
set -eu
cd "$(dirname "$0")/.."
message_cjson_source=${1:?Usage: sh scripts/test_message_payload.sh /path/to/cJSON-1.7.19}
message_test_dir=$(mktemp -d /tmp/vfd-message-payload.XXXXXX)
trap 'rm -rf "$message_test_dir"' EXIT HUP INT TERM
"${CC:-cc}" -std=c99 -Wno-deprecated-declarations -fsanitize=address,undefined -c "$message_cjson_source/cJSON.c" -o "$message_test_dir/cJSON.o"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -Iinclude -I"$message_cjson_source" test/message_payload_check.cpp "$message_test_dir/cJSON.o" -o "$message_test_dir/check"
"$message_test_dir/check"

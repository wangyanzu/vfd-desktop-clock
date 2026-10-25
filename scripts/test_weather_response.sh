#!/bin/sh
# Pass a cJSON 1.7.19 source directory (same version as the ESP32 SDK).
set -eu
cd "$(dirname "$0")/.."
weather_cjson_source=${1:?Usage: sh scripts/test_weather_response.sh /path/to/cJSON-1.7.19}
weather_test_dir=$(mktemp -d /tmp/vfd-weather-response.XXXXXX)
trap 'rm -rf "$weather_test_dir"' EXIT HUP INT TERM
"${CC:-cc}" -std=c99 -Wno-deprecated-declarations -fsanitize=address,undefined -c "$weather_cjson_source/cJSON.c" -o "$weather_test_dir/cJSON.o"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -Iinclude -I"$weather_cjson_source" test/weather_response_check.cpp "$weather_test_dir/cJSON.o" -o "$weather_test_dir/check"
"$weather_test_dir/check"

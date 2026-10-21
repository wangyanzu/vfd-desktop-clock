#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
task_test_binary=$(mktemp /tmp/vfd-clock-test.XXXXXX)
trap 'rm -f "$task_test_binary"' EXIT HUP INT TERM
for task_test_source in test/*_test.cpp; do
  "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -Iinclude "$task_test_source" -o "$task_test_binary"
  "$task_test_binary"
done

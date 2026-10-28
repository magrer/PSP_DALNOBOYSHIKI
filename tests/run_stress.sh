#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
gcc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer src/sim.c tests/test_stress.c -lm -o /tmp/rh_stress_test
case "$(uname -s)" in Darwin) leaks=0 ;; *) leaks=1 ;; esac
ASAN_OPTIONS=detect_leaks=$leaks /tmp/rh_stress_test

#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
gcc -std=c11 -O2 -Wall -Wextra -Werror src/sim.c tests/test_sim.c -lm -o /tmp/rh_sim_test
/tmp/rh_sim_test

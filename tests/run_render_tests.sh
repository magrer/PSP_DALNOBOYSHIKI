#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
gcc -std=c11 -O1 -g -Wall -Wno-unused-parameter -I tests/psp_mock src/sim.c src/render_psp.c tests/mock_backend.c tests/test_render.c -lm -o /tmp/rh_render_test
/tmp/rh_render_test

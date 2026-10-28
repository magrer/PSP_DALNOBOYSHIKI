#!/bin/sh
set -eu
cd "$(dirname "$0")"
if ! command -v docker >/dev/null; then
 echo 'Docker Desktop is required: https://www.docker.com/products/docker-desktop/' >&2
 exit 1
fi
docker run --rm --platform linux/amd64 -v "$PWD:/source" -w /source pspdev/pspdev:v20261001 make -j2
[ -f EBOOT.PBP ] && echo 'Built: EBOOT.PBP'

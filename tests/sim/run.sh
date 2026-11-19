#!/bin/bash
# Unit tests (maths, expressions), then the simulator scripts in order against
# a fresh save folder. Screenshots go to build/shots/. Non-zero exit on failure.
set -e
cd "$(dirname "$0")/../.."
make -s -C src/sim
./build/sim/mathtest | tail -1
./build/sim/exprtest
out=build/shots
rm -rf "$out" && mkdir -p "$out"
status=0
for t in tests/sim/[0-9]*.txt; do
	echo "== $t"
	./build/sim/sim "$t" "$out" "$out" >/dev/null || status=1
done
exit $status

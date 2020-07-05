#!/bin/sh
set -eu

make > /dev/null

ITERATIONS=${1:-10}
for i in $(seq $ITERATIONS);
do
	sudo ./uclamp_test
done

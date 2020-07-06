#!/bin/sh
set -eu

SUDO=""

if [ -n "$(which make)" ]; then
	make > /dev/null
fi

if [ -n "$(which sudo)" ]; then
	SUDO=sudo
fi

ITERATIONS=${1:-10}
for i in $(seq $ITERATIONS);
do
	$SUDO ./uclamp_test
done

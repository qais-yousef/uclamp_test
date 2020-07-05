#!/bin/sh
set -eu

make > /dev/null

for i in $(seq 10);
do
	sudo ./uclamp_test
done

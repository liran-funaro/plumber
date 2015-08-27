#!/bin/bash

for T in $(seq 2); do
	for i in {4..2047}; do
		success="1"
		while [ $success -eq 1 ]; do
			echo "Testing set: $i"
			sudo cgexec -g cpuset,intel_rdt:plumber sudo ./Release/CleanCache $i
			success=$(echo $?)
		done
	done
done
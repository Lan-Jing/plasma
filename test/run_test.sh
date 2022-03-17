#!/bin/bash

spack load openmpi

MODE=""
if [[ $# < 1 || $1 == "debug" ]]; then
	MODE="debug"
else
	MODE="ib"
fi

pushd .. && make clean && make $MODE && popd

`which mpirun` --mca btl ^openib -np 2 --host cpn245,cpn250 ./test_2nodes.sh cpn245
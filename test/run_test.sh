#!/bin/bash

source $HOME/asc21/lj/spack/share/spack/setup-env.sh
spack load openmpi

MODE=""
if [[ $# < 1 || $1 == "tcp" ]]; then
	MODE="all"
else
	MODE="ib"
fi

pushd .. && make clean && make $MODE && popd

if [[ $# < 2 || $2 == "2" ]]; then
	`which mpirun` --mca btl ^openib -np 2 --host cpn14,cpn21 \
	./test_nodes.sh $1 cpn14
else
	`which mpirun` --mca btl ^openib -np 4 --host cpn14,cpn21,cpn209,cpn252 \
	./test_nodes.sh $1 cpn14
fi
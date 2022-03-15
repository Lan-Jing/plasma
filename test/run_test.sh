#!/bin/bash

spack load openmpi
pushd .. && make ib && popd

`which mpirun` --mca btl ^openib -np 2 --host cpn245,cpn250 ./test_2nodes.sh cpn245
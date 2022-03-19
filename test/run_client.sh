#!/bin/bash

ROOT_DIR=$(realpath `pwd`/..)

export LD_LIBRARY_PATH=$ROOT_DIR/build:$LD_LIBRARY_PATH
export PATH=/usr/sbin:$PATH

HOSTNAME=`hostname`

if [[ $HOSTNAME == cpn245 ]]; then
	RANK=0
else
	RANK=1
fi

export IP_ADDR=`hostname --ip-address`
export STORE_IPC_PORT=1000$RANK
export MANAGER_PORT=12345

$ROOT_DIR/build/mpi_tests -s $STORE_IPC_PORT -m $IP_ADDR -p $MANAGER_PORT

# 89.72.35.45
# 89.72.35.50
# ../common/thirdparty/redis-3.2.3/src/redis-server --protected-mode no &
# ../build/plasma_store -s $STORE_IPC_PORT &
# ../build/plasma_manager -s $STORE_IPC_PORT -m $IP_ADDR -p $MANAGER_PORT -d "$MASTER_IP:6379" 2>manager_$RANK.log &
# perf record -g -p `pidof plasma_manager` -o perf_0.data
# `which mpirun` --mca btl ^openib -np 2 --host cpn245,cpn250 ./run_client.sh
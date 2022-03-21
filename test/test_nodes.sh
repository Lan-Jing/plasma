#!/bin/bash

ROOT_DIR=$(realpath `pwd`/..)

# So that we can see libplasma_client.so
export LD_LIBRARY_PATH=$ROOT_DIR/build:$LD_LIBRARY_PATH
export PATH=/usr/sbin:$PATH

source $HOME/asc21/lj/spack/share/spack/setup-env.sh
spack load numactl

# Avoid conflict on a shared file system.
RANK=$OMPI_COMM_WORLD_RANK
STORE_IPC_PORT=1000$RANK
MANAGER_PORT=12345

HOSTNAME=`hostname`
IP_ADDR=`hostname --ip-address`

if [[ $# < 2 ]]; then
	echo "Specify benchmark mode(tcp/ib)"
	echo "And hostname of the master server"
	exit 0
fi

# Resolve IP of the master node.
MASTER_IP=`getent hosts $2 | awk '{ print $1 }'`

# kill all processes listening on ports before starting redis/manager/store.
kill -9 `lsof -t -i :6379`  || true
pkill plasma || true

if [[ $HOSTNAME == $2 ]]; then
	printf "Master Process on %s\n" "$HOSTNAME"
	
	# Remove all Unix Domain Socket
	rm 1000* || true
	rm *data || true
	$ROOT_DIR/common/thirdparty/redis-3.2.3/src/redis-server --protected-mode no &
else
	printf "Slave Process on  %s\n" "$HOSTNAME"
fi

# Binding numa works
if [[ $1 == "tcp" ]]; then
	# NUMA_COMMAND=""
	NUMA_COMMAND="numactl --cpunodebind=1"
else
	NUMA_COMMAND="numactl --cpunodebind=1"
fi

sleep 1 # wait a while for redis setup	
$NUMA_COMMAND $ROOT_DIR/build/plasma_store -s $STORE_IPC_PORT &
$NUMA_COMMAND $ROOT_DIR/build/plasma_manager -s $STORE_IPC_PORT -m $IP_ADDR -p $MANAGER_PORT -d "$MASTER_IP:6379" 2>manager_$RANK.log &
$NUMA_COMMAND $ROOT_DIR/build/mpi_tests -s $STORE_IPC_PORT -m $IP_ADDR -p $MANAGER_PORT
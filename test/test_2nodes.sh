#!/bin/bash

ROOT_DIR=$(realpath `pwd`/..)

# Avoid conflict on a shared file system.
RANK=$OMPI_COMM_WORLD_RANK
STORE_IPC_PORT=1000$RANK
HOSTNAME=`hostname`
IP_ADDR=`hostname --ip-address`

if [[ $# < 1 ]]; then
	echo "Specify hostname of the master server."
	exit 0
fi

# Resolve IP of the master node.
MASTER_IP=`getent hosts $1 | awk '{ print $1 }'`

if [[ $HOSTNAME == $1 ]]; then
	printf "Master Process on %s\n" "$HOSTNAME"
	rm 1000*

	# kill all processes listening on port 6379 before starting redis.
	kill -9 `lsof -t -i :6379` 
	$ROOT_DIR/common/thirdparty/redis-3.2.3/src/redis-server &
	sleep 1 # wait a while for redis setup	

	$ROOT_DIR/build/plasma_store -s $STORE_IPC_PORT &
	$ROOT_DIR/build/plasma_manager -s $STORE_IPC_PORT -m $IP_ADDR -p 12345 -d "$MASTER_IP:6379" &
else
	printf "Slave Process on  %s\n" "$HOSTNAME"
	sleep 1 # wait a while for redis setup

	$ROOT_DIR/build/plasma_store -s $STORE_IPC_PORT &
	$ROOT_DIR/build/plasma_manager -s $STORE_IPC_PORT -m $IP_ADDR -p 12345 -d "$MASTER_IP:6379" &
fi
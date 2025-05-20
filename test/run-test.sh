#!/bin/sh -e

ip link add dev vcan0 type vcan
ip link add dev vcan1 type vcan
ip link add vxcan0 type vxcan peer name vxcan1

./test/gwtest --add-rule
./test/gwtest --delete-rule
./test/gwtest --arg-error

ip link del vxcan0
ip link del vcan0
ip link del vcan1


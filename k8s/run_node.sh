#!/bin/bash
docker run -it --name pbspro_node_$1 -h pbs_$1 -e PBS_START_MOM=1 -e PBS_START_SERVER=0 -e PBS_START_SCHED=0 -e PBS_START_COMM=0 pbspro/pbspro_node:centos7 bash
#docker rm pbspro_node_$1
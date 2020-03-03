#!/bin/bash
docker run -it --name pbspro_server -h pbs_sn -e PBS_START_MOM=0 -e PBS_START_SERVER=1 -e PBS_START_SCHED=1 -e PBS_START_COMM=1 pbspro/pbspro_server:centos7 bash
#docker rm pbspro_server
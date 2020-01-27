#!/bin/bash
docker build . -f Dockerfile.base -t pbspro/pbspro_base_202001:centos7
cp -p /usr/bin/docker-init .
docker build --build-arg PBSPRO_GITHUB_PATH=ericpershey . -f Dockerfile.server -t pbspro/pbspro_server_202001:centos7 --force-rm
# raise ulimit
#   http://community.pbspro.org/t/increasing-the-open-files-limit-across-all-nodes/1361
docker build --build-arg PBSPRO_GITHUB_PATH=ericpershey . -f Dockerfile.node -t pbspro/pbspro_node_202001:centos7 --ulimit memlock=9007199254740991:9007199254740991 --force-rm
# --no-cache
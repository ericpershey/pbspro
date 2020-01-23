#!/bin/bash
docker build . -f Dockerfile.base -t pbspro/pbspro_base_202001:centos7
docker build . -f Dockerfile.server --build-arg pbspro_repo=ericpershey -t pbspro/pbspro_server_202001:centos7 --force-rm --no-cache
docker build . -f Dockerfile.node --build-arg pbspro_repo=ericpershey -t pbspro/pbspro_node_202001:centos7 --force-rm --no-cache
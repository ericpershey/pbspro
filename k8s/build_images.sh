#!/bin/bash
docker build . -f Dockerfile.base -t pbspro/pbspro_base_202001:centos7
docker build --build-arg PBSPRO_GITHUB_PATH=ericpershey . -f Dockerfile.server -t pbspro/pbspro_server_202001:centos7  # --force-rm
docker build --build-arg PBSPRO_GITHUB_PATH=ericpershey . -f Dockerfile.node -t pbspro/pbspro_node_202001:centos7  # --force-rm
# --no-cache
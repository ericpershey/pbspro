#!/bin/bash
export PBS_IMAGE_DATE=202002
export PBS_IMAGE_TAG=debian9
docker build . -f Dockerfile.base -t pbspro/pbspro_base_$PBS_IMAGE_DATE:$PBS_IMAGE_TAG --force-rm --no-cache
docker build --build-arg PBSPRO_GITHUB_PATH=ericpershey . -f Dockerfile.pbs -t pbspro/pbspro_build_$PBS_IMAGE_DATE:$PBS_IMAGE_TAG --force-rm --no-cache
cp -p /usr/bin/docker-init .
docker build  . -f Dockerfile.server -t pbspro/pbspro_server_$PBS_IMAGE_DATE:$PBS_IMAGE_TAG --force-rm --no-cache
docker build  . -f Dockerfile.node -t pbspro/pbspro_node_$PBS_IMAGE_DATE:$PBS_IMAGE_TAG --ulimit memlock=9007199254740991:9007199254740991 --force-rm --no-cache

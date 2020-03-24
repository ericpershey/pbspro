#!/bin/bash
export PBS_IMAGE_DATE=`date "+%Y%m"`
export PBS_GITHUB_PATH="ericpershey"
export PBS_GITHUB_BRANCH="k8s_build"
export PBS_GITHUB_REPO="https://github.com/${PBS_GITHUB_PATH}/pbspro.git"
declare -a PBS_IMAGE_TAG_ARRAY=("centos7" "debian9")
#declare -a PBS_IMAGE_TAG_ARRAY=("debian9")
# This is not used, but set in pbspro-cluster-pod-server.yml
# and pbspro-cluster-statefulset-node.yml
export PBS_K8S_DEFAULT_IMAGE="pbspro/pbspro_server_centos7"
test -n "$UID" || export UID=`id -ur`
test -n "$USER" || export USER=`id -un`
export PBS_SOURCE_DIR=$(cd .. && echo $PWD)
export PBS_BUILD_DIR=/tmp/build-pbspro

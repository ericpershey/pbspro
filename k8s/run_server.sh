#!/bin/bash -x
source ./set_environment_local.sh
source ./build_docker_containers.sh
set -x
for PBS_IMAGE_TAG in "${PBS_IMAGE_TAG_ARRAY[@]}"
do
	export PBS_CONTAINER_NAME="pbsdev-${PBS_IMAGE_TAG}"
	export PBS_HOST_NAME="${PBS_CONTAINER_NAME}"
	echo "Running container ${PBS_CONTAINER_NAME}"
	docker cp ./run_ptl.sh ${PBS_CONTAINER_NAME}:/run_ptl.sh
	docker exec --privileged \
		--env PBS_SOURCE_USER=$USER \
        -e PBS_SOURCE_DIR=${PBS_SOURCE_DIR} \
        -e PBS_BUILD_DIR=${PBS_BUILD_DIR} \
		${PBS_CONTAINER_NAME} /bin/bash /run_ptl.sh
    docker exec -it \
        --env PBS_SOURCE_USER=$USER \
        -e PBS_SOURCE_DIR=${PBS_SOURCE_DIR} \
        -e PBS_BUILD_DIR=${PBS_BUILD_DIR} \
        -e PBS_START_MOM=0 \
        -e PBS_START_SERVER=1 \
        -e PBS_START_SCHED=1 \
        -e PBS_START_COMM=1 \
        ${PBS_CONTAINER_NAME} bash
	exit #exit instead of running any more.
done

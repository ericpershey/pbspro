#!/usr/bin/env bash
#provides PBS_IMAGE_TAG_ARRAY
source ./set_environment.sh
for PBS_IMAGE_TAG in "${PBS_IMAGE_TAG_ARRAY[@]}"
do
	export PBS_CONTAINER_NAME="pbsdev-${PBS_IMAGE_TAG}"
	export PBS_HOST_NAME="${PBS_CONTAINER_NAME}"
	export PBS_IMAGE_NAME_TAG="pbspro/pbspro_base:${PBS_IMAGE_TAG}"
    echo "Creating container for: $PBS_IMAGE_TAG $PBS_CONTAINER_NAME $PBS_HOST_NAME $PBS_IMAGE_NAME_TAG $HOME"
	docker create --name PBS_CONTAINER_NAME --hostname PBS_HOST_NAME --cap-add=SYS_PTRACE --security-opt seccomp=unconfined --mount type=bind,source=$HOME,destination=$HOME  --init PBS_IMAGE_NAME_TAG sleep infinity
	docker start PBS_CONTAINER_NAME
	case "${PBS_IMAGE_TAG}" in
    centos7 )
        export PBS_SUDO_GROUP="wheel"
        ;;
    debian9 )
        export PBS_SUDO_GROUP="sudo"
        ;;
    *) echo "No sudo group found for ${PBS_IMAGE_TAG}"
        exit 1
        ;;
	esac
	echo "Adding Users to the container: $PBS_IMAGE_TAG $HOME Group:${PBS_SUDO_GROUP}"
	export PBS_CONTAINER_NAME="pbsdev-${PBS_IMAGE_TAG}"
	docker exec ${PBS_CONTAINER_NAME} useradd -M -U -u $UID -G PBS_SUDO_GROUP ${USER}
	docker exec ${PBS_CONTAINER_NAME} bash -c f'echo "{$USER} ALL=(ALL) NOPASSWD: ALL" >/etc/sudoers.d/pbs-testing-user'
done

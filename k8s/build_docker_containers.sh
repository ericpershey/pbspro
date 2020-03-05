#!/bin/bash -ex
# set_environment must be called first
for PBS_IMAGE_TAG in "${PBS_IMAGE_TAG_ARRAY[@]}"
do
	export PBS_CONTAINER_NAME="pbsdev-${PBS_IMAGE_TAG}"
	export PBS_HOST_NAME="${PBS_CONTAINER_NAME}"
	export PBS_IMAGE_NAME_TAG="pbspro/pbspro_base:${PBS_IMAGE_TAG}"
	docker stop ${PBS_CONTAINER_NAME} || true && docker rm ${PBS_CONTAINER_NAME} || true
    echo "Creating container for: $PBS_IMAGE_TAG $PBS_CONTAINER_NAME $PBS_HOST_NAME $PBS_IMAGE_NAME_TAG $HOME"
	docker create --name ${PBS_CONTAINER_NAME} \
		--hostname ${PBS_HOST_NAME} \
		--cap-add=SYS_PTRACE --security-opt seccomp=unconfined \
		--mount type=bind,source=$HOME,destination=$HOME \
		--init ${PBS_IMAGE_NAME_TAG} \
		sleep infinity
	docker start ${PBS_CONTAINER_NAME}
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
	docker exec ${PBS_CONTAINER_NAME} useradd -M -U -u $UID -G ${PBS_SUDO_GROUP} ${USER}
	docker exec ${PBS_CONTAINER_NAME} bash -c 'echo "{$USER} ALL=(ALL) NOPASSWD: ALL" >/etc/sudoers.d/pbs-testing-user'
	#docker stop ${PBS_CONTAINER_NAME}
	echo "Container ${PBS_CONTAINER_NAME} is ready to be started."
done

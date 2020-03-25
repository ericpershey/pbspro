#!/bin/bash -ex
# set_environment must be called first
for PBS_IMAGE_TAG in "${PBS_IMAGE_TAG_ARRAY[@]}"
do
	echo "Building images for: $PBS_IMAGE_TAG"
	docker build . -f Dockerfile.base.${PBS_IMAGE_TAG} -t pbspro/pbspro_base:${PBS_IMAGE_TAG} # --force-rm --no-cache
	if test ! -f docker-init ; then
		cid_file=.docker-init-$(hostname)-$$
		rm -f $cid_file
    	docker run --init --cidfile $cid_file pbspro/pbspro_base:${PBS_IMAGE_TAG} \
			cp /sbin/docker-init /tmp/
		docker cp $(cat $cid_file):/tmp/docker-init .
		docker rm $(cat $cid_file)
		rm -f $cid_file
	fi
	docker build . -f Dockerfile.pbs \
		--build-arg PBS_GITHUB_PATH=${PBS_GITHUB_PATH} \
		--build-arg PBS_GITHUB_REPO=${PBS_GITHUB_REPO} \
		--build-arg PBS_GITHUB_BRANCH=${PBS_GITHUB_BRANCH} \
		--build-arg PBS_IMAGE_DATE=${PBS_IMAGE_DATE} \
		--build-arg PBS_IMAGE_TAG=${PBS_IMAGE_TAG} \
		-t pbspro/pbspro_server_${PBS_IMAGE_TAG} --force-rm --no-cache
	docker build . -f Dockerfile.pbs \
		--build-arg PBS_GITHUB_PATH=${PBS_GITHUB_PATH} \
		--build-arg PBS_GITHUB_REPO=${PBS_GITHUB_REPO} \
		--build-arg PBS_GITHUB_BRANCH=${PBS_GITHUB_BRANCH} \
		--build-arg PBS_IMAGE_DATE=${PBS_IMAGE_DATE} \
		--build-arg PBS_IMAGE_TAG=${PBS_IMAGE_TAG} \
		-t pbspro/pbspro_node_${PBS_IMAGE_TAG} --ulimit memlock=9007199254740991:9007199254740991 --force-rm --no-cache
done

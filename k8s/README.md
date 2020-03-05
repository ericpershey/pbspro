# Building and Running with Kubernetes

Note: There are some timing issues with startup and you may need to run
run_cluster multiple times to get all the pbs services running and added.

## Kubernetes Cluster Start

To start the cluster run the following:
```bash
./build_docker_images_minikube.sh
./run_cluster.sh
kubectl exec -it pbspro-server -- /opt/pbs/bin/pbsnodes -a
kubectl exec -it pbspro-server -- /bin/bash
```

# Docker Server Start
```bash
./build_docker_images_local.sh
./run_server.sh
```

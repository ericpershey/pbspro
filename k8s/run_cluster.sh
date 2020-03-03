#!/usr/bin/env bash
minikube start --memory 4gb
eval $(minikube docker-env)
KUBE_IP=$(minikube ip)
echo $KUBE_IP
# GENERIC: Start the PBS Pro Cluster
kubectl apply -f pbspro-cluster-rbac.yml
# create the service to link them
kubectl apply -f pbspro-cluster-pod-server-service.yml
kubectl apply -f pbspro-cluster-statefulset-node-service.yml
# apply configmaps before the pods
kubectl apply -f pbspro-cluster-job-scripts.yml
kubectl apply -f pbspro-cluster-statefulset-node-configmap.yml
kubectl apply -f pbspro-cluster-pod-server-configmap.yml
# create the server
kubectl apply -f pbspro-cluster-pod-server.yml
# create the nodes
# FIXME: it takes some time to come up, lets give it a few until we
# figure out how to make a dependency.
sleep 5
kubectl apply -f pbspro-cluster-statefulset-node.yml
POD_PBS_SERVER=$(kubectl get pod -l app=pbspro-server -o jsonpath="{.items[0].metadata.name}")
POD_PBS_NODE=$(kubectl get pod -l app=pbspro-node -o jsonpath="{.items[0].metadata.name}")
echo $POD_PBS_SERVER $POD_PBS_NODE
kubectl exec -it $POD_PBS_SERVER -- cat /etc/resolv.conf
kubectl exec -it $POD_PBS_NODE -- cat /etc/resolv.conf
kubectl exec -it $POD_PBS_SERVER -- /bin/bash
#kubectl exec -it $POD_PBS_NODE -- /bin/bash

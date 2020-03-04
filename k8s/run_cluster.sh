#!/bin/bash -xe
source ./set_environment.sh
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
sleep 5
POD_PBS_SERVER=$(kubectl get pod -l app=pbspro-server -o jsonpath="{.items[0].metadata.name}")
POD_PBS_NODE=$(kubectl get pod -l app=pbspro-node -o jsonpath="{.items[0].metadata.name}")
echo ${POD_PBS_SERVER} ${POD_PBS_NODE}
kubectl exec -it ${POD_PBS_SERVER} -- cat /etc/resolv.conf
kubectl exec -it ${POD_PBS_NODE} -- cat /etc/resolv.conf
kubectl cp ./pbspro_create_cluster.sh ${POD_PBS_SERVER}:/pbspro_create_cluster.sh
kubectl exec -it ${POD_PBS_SERVER} -- /bin/bash /pbspro_create_cluster.sh
#kubectl exec -it $POD_PBS_SERVER -- /bin/bash
#kubectl exec -it $POD_PBS_NODE -- /bin/bash
sleep 5
for i in {0..7}
do
   kubectl exec -it pbspro-node-$1 -- /bin/bash /etc/init.d/pbs start
done
kubectl get pods --all-namespaces
echo "Now you can run something like the following:"
echo "kubectl exec -it $POD_PBS_NODE -- /bin/bash"
echo "kubectl exec -it $POD_PBS_SERVER -- /bin/bash"
#minikube stop
#minikube delete

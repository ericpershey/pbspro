#!/bin/bash
source ./set_environment.sh
if ! minikube status ; then
    minikube start --memory 4gb
fi
eval $(minikube docker-env)

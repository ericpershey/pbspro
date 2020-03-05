#!/bin/bash
source ./set_environment.sh
minikube start --memory 4gb
eval $(minikube docker-env)

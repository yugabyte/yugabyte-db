## Prerequisites

a) You must have Minikube installed on your localhost. [Follow these instructions](https://kubernetes.io/docs/tasks/tools/install-minikube/) to install Minikube along with its pre-requisites.

We will be using the `StatefulSets` feature of Kubernetes, so you should have a version that supports this (preferably 1.8+). Run the version command as shown below to verify the version.

```
$ kubectl version
Client Version: version.Info{Major:"1", Minor:"9", GitVersion:"v1.9.1", GitCommit:"3a1c9449a956b6026f075fa3134ff92f7d55f812", GitTreeState:"clean", BuildDate:"2018-01-04T11:52:23Z", GoVersion:"go1.9.2", Compiler:"gc", Platform:"darwin/amd64"}
Server Version: version.Info{Major:"1", Minor:"8", GitVersion:"v1.8.0", GitCommit:"0b9efaeb34a2fc51ff8e4d34ad9bc6375459c4a4", GitTreeState:"dirty", BuildDate:"2017-10-17T15:09:55Z", GoVersion:"go1.8.3", Compiler:"gc", Platform:"linux/amd64"}
```


## Download

Download `yugabyte-k8s-statefulset.yaml`. This will create a local cluster with a replication factor of 3.

```sh
$ mkdir ~/yugabyte && cd ~/yugabyte
$ wget https://downloads.yugabyte.com/kubernetes/yugabyte-statefulset.yaml
```

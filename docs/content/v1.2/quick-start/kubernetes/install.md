## Prerequisites

You must have [Minikube](https://github.com/kubernetes/minikube) installed on your localhost. [Follow these instructions](https://kubernetes.io/docs/tasks/tools/install-minikube/) to install Minikube along with its prerequisites.

We will be using the [StatefulSets](https://kubernetes.io/docs/concepts/workloads/controllers/statefulset/) workload API of Kubernetes, so you should have a version that supports this (preferably 1.8+). Run the version commands as shown below to verify the version.

```sh
$ minikube version
```

```
minikube version: v0.25.0
```

## Start Kubernetes

Start Kubernetes via Minikube with the following command.

```sh
$ minikube start
```

Review Kubernetes dashboard with the following command.

```sh
$ minikube dashboard
```

Also confirm that your `kubectl` is configured correctly.

```sh
$ kubectl version
```

```
Client Version: version.Info{Major:"1", Minor:"9", GitVersion:"v1.9.1", ...}
Server Version: version.Info{Major:"1", Minor:"8", GitVersion:"v1.8.0", ...}
```

## Download

Download `yugabyte-statefulset.yaml`. This will create a local YugaByte DB cluster on Kubernetes with a replication factor of 3.

```sh
$ mkdir ~/yugabyte && cd ~/yugabyte
```

```sh
$ wget https://raw.githubusercontent.com/YugaByte/yugabyte-db/master/cloud/kubernetes/yugabyte-statefulset-rf-1.yaml
```

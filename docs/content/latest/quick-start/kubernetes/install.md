## Prerequisites

You need to have [Minikube](https://github.com/kubernetes/minikube) installed on your localhost machine.

- The Kubernetes version used by Minikube should be v1.13.0 or later. The default Kubernetes version being used by Minikube displays when you run the `minikube start` command.
- To install Minikube, see [Install Minikube](https://kubernetes.io/docs/tasks/tools/install-minikube/) in the Kubernetes documentation.

## Start Kubernetes

Start Kubernetes using Minikube by running the following command.

```sh
$ minikube start
```

Review Kubernetes dashboard by running the following command.

```sh
$ minikube dashboard
```

Confirm that your `kubectl` is configured correctly by running the following command.

```sh
$ kubectl version
```

```
Client Version: version.Info{Major:"1", Minor:"9", GitVersion:"v1.9.1", ...}
Server Version: version.Info{Major:"1", Minor:"8", GitVersion:"v1.8.0", ...}
```

## Download

Download `yugabyte-statefulset.yaml`. You will use this YAML file to create a YugabyteDB cluster running inside Kubernetes with a replication factor of 1.

```sh
$ mkdir ~/yugabyte && cd ~/yugabyte
```

```sh
$ wget https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/cloud/kubernetes/yugabyte-statefulset-rf-1.yaml
```

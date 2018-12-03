---
title: Helm Chart
linkTitle: Helm Chart
description: Helm Chart
aliases:
  - /deploy/kubernetes/helm-chart/
menu:
  latest:
    identifier: helm-chart
    parent: deploy-kubernetes
    weight: 621
---

## Introduction

[Helm](https://helm.sh/) is an open source packaging tool that helps install applications and services on Kubernetes. It uses a packaging format called `charts`. Charts are a collection of YAML templates that describe a related set of Kubernetes resources.

## Prerequisites

You must have a Kubernetes cluster that has Helm configured. If you have not installed Helm client and server (aka Tiller) yet, follow the instructions [here](https://docs.helm.sh/using_helm/#installing-helm).

The YugaByte DB Helm chart documented here has been tested with the following software versions:

- Kubernetes 1.10+
- Helm 2.8.0+
- YugaByte DB Docker Images 1.1.0+
- Kubernetes nodes where a total of 12 CPU cores and 45 GB RAM can be allocated to YugaByte DB. This can be 3 nodes with 4 CPU core and 15 GB RAM allocated to YugaByte DB.
- For optimal performance, ensure to set the appropriate [system limits using `ulimit`](../../manual-deployment/system-config/#setting-ulimits/) on each node in your Kubernetes cluster.

Confirm that your `helm` is configured correctly.

```{.sh .copy .separator-dollar}
$ helm version
```
```sh
Client: &version.Version{SemVer:"v2.10.0", GitCommit:"...", GitTreeState:"clean"}
Server: &version.Version{SemVer:"v2.10.0", GitCommit:"...", GitTreeState:"clean"}
```

## Create Cluster

### Clone YugaByte DB Project

For creating the cluster, you have to first clone the yugabyte-db project and then create a YugaByte service account in your Kubernetes cluster.

```{.sh .copy .separator-dollar}
$ git clone https://github.com/YugaByte/yugabyte-db.git
```

```{.sh .copy .separator-dollar}
$ cd ./yugabyte-db/cloud/kubernetes/helm/
```

```{.sh .copy .separator-dollar}
$ kubectl create -f yugabyte-rbac.yaml
```
```sh
serviceaccount/yugabyte-helm created
clusterrolebinding.rbac.authorization.k8s.io/yugabyte-helm created
```

### Initialize Helm

Initialize `helm` with the service account but use the `--upgrade` flag to ensure that you can upgrade any previous initializations you may have made.

```{.sh .copy .separator-dollar}
$ helm init --service-account yugabyte-helm --upgrade --wait
```
```sh
$HELM_HOME has been configured at /Users/<user>/.helm.

Tiller (the Helm server-side component) has been upgraded to the current version.
Happy Helming!
```

### Install YugaByte DB

Install YugaByte DB in the Kubernetes cluster using the command below.

```{.sh .copy .separator-dollar}
$ helm install yugabyte --namespace yb-demo --name yb-demo --wait
```

If you are running in a resource-constrained environment or a local environment such as minikube, you will have to change the default resource requirements by using the command below. See next section for a detailed description of these resource requirements.

```{.sh .copy .separator-dollar}
$ helm install yugabyte --set resource.master.requests.cpu=0.1,resource.master.requests.memory=0.2Gi,resource.tserver.requests.cpu=0.1,resource.tserver.requests.memory=0.2Gi --namespace yb-demo --name yb-demo
```

## Check Cluster Status

You can check the status of the cluster using various commands noted below.

```{.sh .copy .separator-dollar}
$ helm status yb-demo
```
```sh
LAST DEPLOYED: Fri Oct  5 09:04:46 2018
NAMESPACE: yb-demo
STATUS: DEPLOYED

RESOURCES:
==> v1/Service
NAME          TYPE          CLUSTER-IP      EXTERNAL-IP  PORT(S)                              AGE
yb-tservers   ClusterIP     None            <none>       7100/TCP,9000/TCP,6379/TCP,9042/TCP  7s
yb-masters    ClusterIP     None            <none>       7100/TCP,7000/TCP                    7s
yb-master-ui  LoadBalancer  10.106.132.116  <pending>    7000:30613/TCP                       7s

==> v1/StatefulSet
NAME        DESIRED  CURRENT  AGE
yb-master   3        3        7s
yb-tserver  3        3        7s

==> v1/Pod(related)
NAME          READY  STATUS   RESTARTS  AGE
yb-master-0   0/1    Pending  0         7s
yb-master-1   0/1    Pending  0         7s
yb-master-2   0/1    Pending  0         7s
yb-tserver-0  0/1    Pending  0         7s
yb-tserver-1  0/1    Pending  0         7s
yb-tserver-2  0/1    Pending  0         7s

...
```

```{.sh .copy .separator-dollar}
$ kubectl get pods --namespace yb-demo
```
```sh
NAME           READY     STATUS    RESTARTS   AGE
yb-master-0    1/1       Running   0          4m
yb-master-1    1/1       Running   0          4m
yb-master-2    1/1       Running   0          4m
yb-tserver-0   1/1       Running   0          4m
yb-tserver-1   1/1       Running   0          4m
yb-tserver-2   1/1       Running   0          4m
```

```{.sh .copy .separator-dollar}
$ kubectl get services --namespace yb-demo
```
```sh
NAME           TYPE           CLUSTER-IP      EXTERNAL-IP   PORT(S)                               AGE
yb-master-ui   LoadBalancer   10.111.34.175   <pending>     7000:31418/TCP                        1m
yb-masters     ClusterIP      None            <none>        7100/TCP,7000/TCP                     1m
yb-tservers    ClusterIP      None            <none>        7100/TCP,9000/TCP,6379/TCP,9042/TCP   1m
```

You can even check the history of the `yb-demo` helm chart.

```{.sh .copy .separator-dollar}
$ helm history yb-demo
```
```sh
REVISION  UPDATED                   STATUS    CHART           DESCRIPTION     
1         Fri Oct  5 09:04:46 2018  DEPLOYED  yugabyte-latest Install complete
```


## Configure Cluster

### CPU, Memory & Replica Count

The default values for the Helm chart are in the `helm/yugabyte/values.yaml` file. The most important ones are listed below. As noted in the Prerequisites section above, the defaults are set for a 3 nodes Kubernetes cluster each with 4 CPU cores and 15 GB RAM.

```sh
persistentVolume:
  count: 2
  storage: 10Gi
  storageClass: standard

resource:
  master:
    requests:
      cpu: 2
      memory: 7.5Gi
  tserver:
    requests:
      cpu: 2
      memory: 7.5Gi

replicas:
  master: 3
  tserver: 3

partition:
  master: 3
  tserver: 3
```

If you want to change the defaults, you can use the command below. You can even do `helm install` instead of `helm upgrade` when you are installing on a Kubernetes cluster with configuration different than the defaults.

```{.sh .copy .separator-dollar}
$ helm upgrade --set resource.tserver.requests.cpu=8,resource.tserver.requests.memory=15Gi yb-demo ./yugabyte
```

Replica count can be changed using the command below. Note only the tservers need to be scaled in a Replication Factor 3 cluster which keeps the masters count at 3.

```{.sh .copy .separator-dollar}
$ helm upgrade --set replicas.tserver=5 yb-demo ./yugabyte
```

### LoadBalancer for Services

By default, the YugaByte DB helm chart exposes only the master ui endpoint via LoadBalancer. If you wish to expose also the ycql and yedis services via LoadBalancer for your app to use, you could do that in couple of different ways.


If you want individual LoadBalancer endpoint for each of the services (YCQL, YEDIS), run the following command.

```{.sh .copy .separator-dollar}
$ helm install yugabyte -f expose-all.yaml --namespace yb-demo --name yb-demo --wait
```

If you want to create a shared LoadBalancer endpoint for all the services (YCQL, YEDIS), run the following command.

```{.sh .copy .separator-dollar}
$ helm install yugabyte -f expose-all-shared.yaml --namespace yb-demo --name yb-demo --wait
```

## Upgrade Cluster

You can perform rolling upgrades on the YugaByte DB cluster with the following command. Change the `Image.tag` value to any valid tag from [YugaByte DB's listing on the Docker Hub registry](https://hub.docker.com/r/yugabytedb/yugabyte/tags/). By default, the `latest` Docker image is used for the install.

```{.sh .copy .separator-dollar}
$ helm upgrade yb-demo yugabyte --set Image.tag=1.1.0.3-b6 --wait
```

## Delete Cluster

Deleting the cluster involves purging the helm chart followed by deletion of the PVCs.

```{.sh .copy .separator-dollar}
$ helm del --purge yb-demo
```
```{.sh .copy .separator-dollar}
$ kubectl delete pvc --namespace yb-demo --all
```

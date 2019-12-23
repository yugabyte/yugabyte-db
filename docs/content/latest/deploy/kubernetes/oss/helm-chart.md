---
title: Open Source Kubernetes 
linkTitle: Open Source Kubernetes
description: Open Source Kubernetes 
aliases:
  - /latest/deploy/kubernetes/oss/
  - /latest/deploy/kubernetes/helm-chart/
  - /latest/deploy/kubernetes/helm-configuration/
menu:
  latest:
    parent: deploy-kubernetes
    name: Open Source
    identifier: k8s-oss-1
    weight: 621
type: page
isTocNested: true
showAsideToc: true
---


<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="/latest/deploy/kubernetes/oss/helm-chart" class="nav-link active">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Helm chart
    </a>
  </li>
  <li >
    <a href="/latest/deploy/kubernetes/oss/yugabyte-operator" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      YugabyteDB operator
    </a>
  </li>
  <li >
    <a href="/latest/deploy/kubernetes/oss/operator-hub" class="nav-link active">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Operator Hub
    </a>
  </li>
  <li>
    <a href="/latest/deploy/kubernetes/oss/rook-operator" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Rook operator
    </a>
  </li>
</ul>

## Introduction

[Helm](https://helm.sh/) is an open source packaging tool that helps install applications and services on Kubernetes. It uses a packaging format called `charts`. A Helm chart is a package containing all resource definitions necessary to create an instance of a Kubernetes application, tool, or service in a Kubernetes cluster.

## Prerequisites

You must have a Kubernetes cluster that has Helm configured. If you have not installed the Helm client (`helm`), see [Installing Helm](https://helm.sh/docs/intro/install/).

The Helm chart for YugabyteDB (`yugabyte-helm`) has been tested with the following software versions:

- Kubernetes 1.10+
- Helm 2.8.0+ or 3.0.0
- YugabyteDB Docker image (yugabytedb/yugabyte) 1.1.0+
- Kubernetes nodes where a total of 12 CPU cores and 45 GB RAM can be allocated to YugabyteDB. This can be three nodes with 4 CPU core and 15 GB RAM allocated to YugabyteDB.
- For optimal performance, ensure you've set the appropriate [system limits using `ulimit`](../../manual-deployment/system-config/#setting-ulimits/) on each node in your Kubernetes cluster.

Confirm that `helm` is configured correctly.

```sh
$ helm version
```

**Output for Helm 2:**

```
Client: &version.Version{SemVer:"v2.10.0", GitCommit:"...", GitTreeState:"clean"}
Server: &version.Version{SemVer:"v2.10.0", GitCommit:"...", GitTreeState:"clean"}
```

**Output for Helm 3:**

```
version.BuildInfo{Version:"v3.0.0-beta.4", GitCommit:"...", GitTreeState:"dirty", GoVersion:"go1.13.1"}
```

## Create cluster

For Helm 3, jump directly to [Add charts repository](#add-charts-repository) section.

### Create service account (Helm 2 only)

Before you can create the cluster, you need to have a service account that has been granted the `cluster-admin` role. Use the following command to create a `yugabyte-helm` service account granted with the ClusterRole of `cluster-admin`.

```sh
$ kubectl create -f https://raw.githubusercontent.com/yugabyte/charts/master/stable/yugabyte/yugabyte-rbac.yaml
```

```sh
serviceaccount/yugabyte-helm created
clusterrolebinding.rbac.authorization.k8s.io/yugabyte-helm created
```

### Initialize Helm (Helm 2 only)

Initialize `helm` with the service account, but use the `--upgrade` option to ensure that you can upgrade any previous initializations you may have made.

```sh
$ helm init --service-account yugabyte-helm --upgrade --wait
```

```
$HELM_HOME has been configured at `/Users/<user>/.helm`.

Tiller (the Helm server-side component) has been upgraded to the current version.
Happy Helming!
```

### Add charts repository

To add the YugabyteDB charts repository, run the following command.

```sh
$ helm repo add yugabytedb https://charts.yugabyte.com
```

### Fetch updates from the repository

Make sure that you have the latest updates to the repository by running the following command.

```sh
$ helm repo update
```

### Validate the chart version

**For Helm 2:**

```sh
$ helm search yugabytedb/yugabyte
```

**For Helm 3:**

```sh
$ helm search repo yugabytedb/yugabyte
```

**Output:**

```sh
NAME               	CHART VERSION	APP VERSION	DESCRIPTION
yugabytedb/yugabyte	1.3.0        	1.3.0.0-b1 	YugabyteDB is the high-performance distr...
```

### Install YugabyteDB

Install YugabyteDB in the Kubernetes cluster using the command below. By default, this Helm chart will expose only the master UI endpoint using LoadBalancer. If you need to connect external clients, see the section below.

**For Helm 2:**

```sh
$ helm install yugabytedb/yugabyte --namespace yb-demo --name yb-demo --wait
```

**For Helm 3:**

```sh
$ helm install yb-demo yugabytedb/yugabyte --namespace yb-demo --wait
```

If you are running in a resource-constrained environment or a local environment, such as Minikube, you will have to change the default resource requirements by using the command below. See next section for a detailed description of these resource requirements.

**For Helm 2:**

```sh
$ helm install yugabytedb/yugabyte --set resource.master.requests.cpu=0.1,resource.master.requests.memory=0.2Gi,resource.tserver.requests.cpu=0.1,resource.tserver.requests.memory=0.2Gi --namespace yb-demo --name yb-demo
```

**For Helm 3:**

```sh
$ helm install yb-demo yugabytedb/yugabyte --set resource.master.requests.cpu=0.1,resource.master.requests.memory=0.2Gi,resource.tserver.requests.cpu=0.1,resource.tserver.requests.memory=0.2Gi --namespace yb-demo
```

Connect using the YSQL CLI (`ysqlsh`) by running the following command.

```sh
$ kubectl exec -n yb-demo -it yb-tserver-0 /home/yugabyte/bin/ysqlsh -- -h yb-tserver-0.yb-tservers.yb-demo
```

## Check the cluster status

You can check the status of the cluster using various commands noted below.

**For Helm 2:**

```sh
$ helm status yb-demo
```

**For Helm 3:**

```sh
$ helm status yb-demo -n yb-demo
```

**Output**:

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

Check the pods.

```sh
$ kubectl get pods --namespace yb-demo
```

```
NAME           READY     STATUS    RESTARTS   AGE
yb-master-0    1/1       Running   0          4m
yb-master-1    1/1       Running   0          4m
yb-master-2    1/1       Running   0          4m
yb-tserver-0   1/1       Running   0          4m
yb-tserver-1   1/1       Running   0          4m
yb-tserver-2   1/1       Running   0          4m
```

Check the services.

```sh
$ kubectl get services --namespace yb-demo
```

```
NAME           TYPE           CLUSTER-IP      EXTERNAL-IP   PORT(S)                               AGE
yb-master-ui   LoadBalancer   10.111.34.175   <pending>     7000:31418/TCP                        1m
yb-masters     ClusterIP      None            <none>        7100/TCP,7000/TCP                     1m
yb-tservers    ClusterIP      None            <none>        7100/TCP,9000/TCP,6379/TCP,9042/TCP   1m
```

You can even check the history of the `yb-demo` deployment.

**For Helm 2:**

```sh
$ helm history yb-demo
```

**For Helm 3**:

```sh
$ helm history yb-demo -n yb-demo
```

**Output:**

```sh
REVISION  UPDATED                   STATUS    CHART           DESCRIPTION
1         Fri Oct  5 09:04:46 2018  DEPLOYED  yugabyte-1.3.0 Install complete
```

## Connect using YugabyteDB CLIs

To connect and use the YSQL CLI (`ysqlsh`), run the following command.

```sh
$ kubectl exec -n yb-demo -it yb-tserver-0 /home/yugabyte/bin/ysqlsh -- -h yb-tserver-0.yb-tservers.yb-demo
```

To connect and use the YCQL CLI (`cqlsh`), run the following command.

```sh
$ kubectl exec -n yb-demo -it yb-tserver-0 /home/yugabyte/bin/cqlsh yb-tserver-0.yb-tservers.yb-demo
```

## Connect using external clients

By default, the YugabyteDB Helm chart will expose only the master UI endpoint using LoadBalancer. If you want to expose YSQL and YCQL services using LoadBalancer for your app to use, you can do the following.

**For Helm 2**:

```sh
helm install yugabytedb/yugabyte -f https://raw.githubusercontent.com/yugabyte/charts/master/stable/yugabyte/expose-all.yaml --namespace yb-demo --name yb-demo --wait
```

**For Helm 3:**

```sh
helm install yb-demo yugabytedb/yugabyte -f https://raw.githubusercontent.com/yugabyte/charts/master/stable/yugabyte/expose-all.yaml --namespace yb-demo --wait
```

To connect an external program, get the load balancer IP address of the corresponding service. The example below shows how to do this for the YSQL and YCQL services.

```sh
$ kubectl get services --all-namespaces
```

```
NAMESPACE     NAME                   TYPE           CLUSTER-IP      EXTERNAL-IP      PORT(S)               AGE
...
yb-demo       yql-service            LoadBalancer   10.47.249.27    35.225.153.213   9042:30940/TCP        2m
yb-demo       ysql-service           LoadBalancer   10.106.28.246   35.225.153.214   5433:30790/TCP        2m
...
```

Any program can use the `EXTERNAL-IP` of the `ysql-service` and `yql-service` to connect to the YSQL and YCQL APIs respectively.

## Configure cluster

Instead of using the default values in the Helm chart, you can also modify the configuration of the YugabyteDB cluster according to your requirements. The following section shows the commands and tags that can be modified to achieve the desired configuration.

### CPU, memory, and replica count

The default values for the Helm chart are in the `helm/yugabyte/values.yaml` file. The most important ones are listed below. As noted in the Prerequisites section above, the defaults are set for a 3-node Kubernetes cluster, each node with 4 CPU cores and 15 GB RAM.

```
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

```sh
$ helm upgrade --set resource.tserver.requests.cpu=8,resource.tserver.requests.memory=15Gi yb-demo ./yugabyte
```

Replica count can be changed using the command below. Note only the tservers need to be scaled in a Replication Factor 3 cluster which keeps the masters count at 3.

```sh
$ helm upgrade --set replicas.tserver=5 yb-demo ./yugabyte
```

### LoadBalancer for services

By default, the YugabyteDB Helm chart exposes only the master UI endpoint using LoadBalancer. If you want to expose the YCQL and YEDIS services using LoadBalancer for your app to use, you could do that in couple of different ways.

If you want an individual LoadBalancer endpoint for each of the services (YCQL, YEDIS), run the following command.

```sh
$ helm install yugabyte -f expose-all.yaml --namespace yb-demo --name yb-demo --wait
```

If you want to create a shared LoadBalancer endpoint for all the services (YCQL, YEDIS), run the following command.

```sh
$ helm install yugabyte -f expose-all-shared.yaml --namespace yb-demo --name yb-demo --wait
```

You can also bring up an internal LoadBalancer (for either YB-Master or YB-TServer services), if required. Just specify the [annotation](https://kubernetes.io/docs/concepts/services-networking/service/#internal-load-balancer) required for your cloud provider. See [Amazon EKS](../../eks/helm-chart/) and [Google Kubernetes Engine](../../gke/helm-chart/) for examples.

### Storage class

In case you want to use a storage class other than the standard class for your deployment, provision the storage class and then pass in the name of the class while running the helm install command.

```sh
$ helm install yugabyte --namespace yb-demo --name yb-demo --set persistentVolume.storageClass=<name of provisioned storage> --wait
```

## Upgrade cluster

You can perform rolling upgrades on the YugabyteDB cluster with the following command. Change the `Image.tag` value to any valid tag from [YugabyteDB's listing on the Docker Hub registry](https://hub.docker.com/r/yugabytedb/yugabyte/tags/). By default, the installation uses the `latest` Docker image. In the examples, the Docker image specified is `2.0.10.0-b4`.

**For Helm 2:**

```sh
$ helm upgrade yb-demo yugabytedb/yugabyte --set Image.tag=2.0.10.0-b4 --wait
```

**For Helm 3:**

```sh
$ helm upgrade yb-demo yugabytedb/yugabyte --set Image.tag=2.0.10.0-b4 --wait -n yb-demo
```


## Delete cluster

To delete the cluster, you need to purge the Helm chart, and then delete the PVCs.

**For Helm 2:**

```sh
$ helm del --purge yb-demo
```

**For Helm 3:**

```sh
$ helm uninstall yb-demo -n yb-demo
```

```sh
$ kubectl delete pvc --namespace yb-demo --all
```

---
title: Use Helm Chart to deploy on Google Kubernetes Engine (GKE)
headerTitle: Google Kubernetes Engine (GKE)
linkTitle: Google Kubernetes Engine (GKE)
description: Use Helm Chart to deploy a single-zone YugabyteDB cluster on Google Kubernetes Engine (GKE).
menu:
  stable:
    parent: deploy-kubernetes-sz
    name: Google Kubernetes Engine
    identifier: k8s-gke-1
    weight: 623
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../helm-chart/" class="nav-link active">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Helm chart
    </a>
  </li>
  <li >
    <a href="../statefulset-yaml/" class="nav-link">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      YAML (remote disk)
    </a>
  </li>
   <li >
    <a href="../statefulset-yaml-local-ssd/" class="nav-link">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      YAML (local disk)
    </a>
  </li>
</ul>

## Prerequisites

You must have a Google Kubernetes Engine (GKE) cluster that has Helm configured. If you have not installed the Helm client (`helm`), see [Installing Helm](https://helm.sh/docs/intro/install/).

The YugabyteDB Helm chart has been tested with the following software versions:

- GKE running Kubernetes 1.20 or later. The Helm chart you use to install YugabyteDB creates three YB-Master and three YB-TServers, each with 2 CPU cores, for a total of 12 CPU cores. This means you need a Kubernetes cluster with more than 12 CPU cores. If the cluster contains three nodes, then each node should have more than 4 cores.

- Helm 3.4 or later.
- For optimal performance, ensure you set the appropriate [system limits using `ulimit`](../../../../manual-deployment/system-config/#ulimits) on each node in your Kubernetes cluster.

The following steps show how to meet these prerequisites:

- Download and install the [Google Cloud SDK](https://cloud.google.com/sdk/downloads/).

- Configure defaults for Google Cloud.

  Execute the followingn command to set the project ID to `yugabyte`. You can change this as needed.

  ```sh
  gcloud config set project yugabyte
  ```

  Execute the followingn command to set  the default compute zone to `us-west1-b`. You can change this as needed.

  ```sh
  gcloud config set compute/zone us-west1-b
  ```

- Install `kubectl`. Refer to kubectl installation instructions for your [operating system](https://kubernetes.io/docs/tasks/tools/).


  Note that GKE is usually two or three major releases behind the upstream or OSS Kubernetes release. This means you have to make sure that you have the latest kubectl version that is compatible across different Kubernetes distributions.

- Ensure that `helm` is installed.

  First, check the Helm version, as follows:

  ```sh
  helm version
  ```

  Expect to see the ouput similar to the following. Note that the `tiller` server-side component has been removed in Helm 3.

  ```output
  version.BuildInfo{Version:"v3.0.3", GitCommit:"ac925eb7279f4a6955df663a0128044a8a6b7593", GitTreeState:"clean", GoVersion:"go1.13.6"}
  ```

## Create a GKE cluster

Create a Kubernetes cluster by running the following command:

```sh
gcloud container clusters create yugabyte --machine-type=n1-standard-8
```

As stated in [Prerequisites](#prerequisites), the default configuration in the YugabyteDB Helm chart requires Kubernetes nodes to have a total of 12 CPU cores and 45 GB RAM allocated to YugabyteDB. This can be three nodes with 4 CPU cores and 15 GB RAM allocated to YugabyteDB. The smallest Google Cloud machine type that meets this requirement is `n1-standard-8` which has 8 CPU cores and 30GB RAM.

## Create a YugabyteDB cluster

Creating a YugabyteDB cluster involves a number of steps.

### Add charts repository

To add the YugabyteDB charts repository, run the following command:

```sh
helm repo add yugabytedb https://charts.yugabyte.com
```

### Fetch updates from the repository

Make sure that you have the latest updates to the repository by running the following command:

```sh
helm repo update
```

### Validate the Chart version

Execute the following command:

```sh
helm search repo yugabytedb/yugabyte --version {{<yb-version version="preview" format="short">}}
```

Expect the following output:

```output
NAME                 CHART VERSION  APP VERSION   DESCRIPTION
yugabytedb/yugabyte  {{<yb-version version="preview" format="short">}}          {{<yb-version version="preview" format="build">}}  YugabyteDB is the high-performance distributed ...
```

### Install YugabyteDB

Run the following commands to create a namespace and then install YugabyteDB:

```sh
kubectl create namespace yb-demo
helm install yb-demo yugabytedb/yugabyte --version {{<yb-version version="preview" format="short">}} --namespace yb-demo --wait
```

## Check the cluster status

You can check the status of the cluster using the following command:

```sh
helm status yb-demo -n yb-demo
```

```output
NAME: yb-demo
LAST DEPLOYED: Thu Feb 13 13:29:13 2020
NAMESPACE: yb-demo
STATUS: deployed
REVISION: 1
TEST SUITE: None
NOTES:
1. Get YugabyteDB Pods by running this command:
  kubectl --namespace yb-demo get pods

2. Get list of YugabyteDB services that are running:
  kubectl --namespace yb-demo get services

3. Get information about the load balancer services:
  kubectl get svc --namespace yb-demo

4. Connect to one of the tablet server:
  kubectl exec --namespace yb-demo -it yb-tserver-0 -- bash

5. Run YSQL shell from inside of a tablet server:
  kubectl exec --namespace yb-demo -it yb-tserver-0 -- ysqlsh -h yb-tserver-0.yb-tservers.yb-demo

6. Cleanup YugabyteDB Pods
  helm delete yb-demo --purge
  NOTE: You need to manually delete the persistent volume
  kubectl delete pvc --namespace yb-demo -l app=yb-master
  kubectl delete pvc --namespace yb-demo -l app=yb-tserver
```

Check the pods, as follows:

```sh
kubectl get pods --namespace yb-demo
```

```output
NAME           READY     STATUS    RESTARTS   AGE
yb-master-0    1/1       Running   0          4m
yb-master-1    1/1       Running   0          4m
yb-master-2    1/1       Running   0          4m
yb-tserver-0   1/1       Running   0          4m
yb-tserver-1   1/1       Running   0          4m
yb-tserver-2   1/1       Running   0          4m
```

Check the services, as follows:

```sh
kubectl get services --namespace yb-demo
```

```output
NAME                 TYPE           CLUSTER-IP      EXTERNAL-IP    PORT(S)                                        AGE
yb-master-ui         LoadBalancer   10.109.39.242   35.225.153.213 7000:31920/TCP                                 10s
yb-masters           ClusterIP      None            <none>         7100/TCP,7000/TCP                              10s
yb-tserver-service   LoadBalancer   10.98.36.163    35.225.153.214 6379:30929/TCP,9042:30975/TCP,5433:30048/TCP   10s
yb-tservers          ClusterIP      None            <none>         7100/TCP,9000/TCP,6379/TCP,9042/TCP,5433/TCP   10s
```

You can even check the history of the `yb-demo` deployment, as follows:

```sh
helm history yb-demo -n yb-demo
```

```output
REVISION  UPDATED                   STATUS    CHART           APP VERSION   DESCRIPTION
1         Tue Apr 21 17:29:01 2020  deployed  yugabyte-{{<yb-version version="preview" format="short">}}  {{<yb-version version="preview" format="build">}}  Install complete
```

## Connect using YugabyteDB shells

To connect and use the YSQL Shell `ysqlsh`, run the following command:

```sh
kubectl exec -n yb-demo -it yb-tserver-0 -- ysqlsh -h yb-tserver-0.yb-tservers.yb-demo
```

To connect and use the YCQL Shell `ycqlsh`, run the following command:

```sh
kubectl exec -n yb-demo -it yb-tserver-0 -- ycqlsh yb-tserver-0.yb-tservers.yb-demo
```

## Connect using external clients

To connect an external program, get the load balancer `EXTERNAL-IP` address of the `yb-tserver-service` service and connect using port 5433 for YSQL or port 9042 for YCQL, as follows:

```sh
kubectl get services --namespace yb-demo
```

```output
NAME                 TYPE           CLUSTER-IP      EXTERNAL-IP        PORT(S)                                        AGE
...
yb-tserver-service   LoadBalancer   10.98.36.163    35.225.153.214     6379:30929/TCP,9042:30975/TCP,5433:30048/TCP   10s
...
```

## Configure cluster

You can configure the cluster using the same commands and options that are described in [Open Source Kubernetes](../../oss/helm-chart/#configure-cluster).

### Independent LoadBalancers

By default, the YugabyteDB Helm chart exposes the client API endpoints, as well as YB-Master UI endpoint using two LoadBalancers. To expose the client APIs using independent LoadBalancers, you can execute the following command:

```sh
helm install yb-demo yugabytedb/yugabyte -f https://raw.githubusercontent.com/yugabyte/charts/master/stable/yugabyte/expose-all.yaml --version {{<yb-version version="preview" format="short">}} --namespace yb-demo --wait
```

You can also bring up an internal LoadBalancer (for either YB-Master or YB-TServer services), if required. You would need to specify the [annotation](https://kubernetes.io/docs/concepts/services-networking/service/#internal-load-balancer) required for your cloud provider. The following command brings up an internal LoadBalancer for the YB-TServer service in Google Cloud Platform:

```sh
helm install yugabyte -f https://raw.githubusercontent.com/yugabyte/charts/master/stable/yugabyte/expose-all.yaml --version {{<yb-version version="preview" format="short">}} --namespace yb-demo --name yb-demo \
  --set annotations.tserver.loadbalancer."cloud\.google\.com/load-balancer-type"=Internal --wait
```

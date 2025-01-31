---
title: Deploy on Google Kubernetes Engine (GKE) using YAML (remote disk)
headerTitle: Google Kubernetes Engine (GKE)
linkTitle: Google Kubernetes Engine (GKE)
description: Deploy a single-zone YugabyteDB cluster on Google Kubernetes Engine (GKE) using YAML (remote disk).
menu:
  preview:
    parent: deploy-kubernetes-sz
    name: Google Kubernetes Engine
    identifier: k8s-gke-2
    weight: 623
aliases:
  - /preview/deploy/kubernetes/gke/statefulset-yaml
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../helm-chart/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Helm chart
    </a>
  </li>
  <li >
    <a href="../statefulset-yaml/" class="nav-link active">
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

Before starting deployment, perform the following:

- Download and install the [Google Cloud SDK](https://cloud.google.com/sdk/downloads/).

  Note that if you install `gcloud` using a package manager (as opposed to downloading and installing it manually), some of the commands will not be supported.

- Install `kubectl` command line tool by running the following command:

  ```sh
  gcloud components install kubectl
  ```

- Configure defaults for `gcloud` by setting the project ID as `yugabyte`. You can change this as needed.

  ```sh
  gcloud config set project yugabyte
  ```

- Set the default compute zone as `us-west1-b`. You can change this as needed.

  ```sh
  gcloud config set compute/zone us-west1-b
  ```

## Create a GKE cluster

Create a private Kubernetes cluster using the following command.

```sh
gcloud container clusters create cluster_name --enable-private-nodes
```

Note that you must set up Cloud NAT for a private Kubernetes cluster in Google Cloud to ensure that your cluster can access the internet while its nodes do not have public IP addresses. Refer to [Configuring Private Google Access and Cloud NAT in Google Cloud Platform (GCP)](https://kloudkraft.medium.com/configuring-private-google-access-and-cloud-nat-in-google-cloud-platform-gcp-3c4406b590b3).

## Create a YugabyteDB cluster

Create a YugabyteDB cluster by running the following command:

```sh
kubectl create -f https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/cloud/kubernetes/yugabyte-statefulset.yaml
```

```output
service "yb-masters" created
statefulset "yb-master" created
service "yb-tservers" created
statefulset "yb-tserver" created
```

## Check the cluster

Execute the following command to see the pods running:

```sh
kubectl get pods
```

```output
NAME           READY     STATUS    RESTARTS   AGE
yb-master-0    1/1       Running   0          3m
yb-master-1    1/1       Running   0          3m
yb-master-2    1/1       Running   0          3m
yb-tserver-0   1/1       Running   0          3m
yb-tserver-1   1/1       Running   0          3m
yb-tserver-2   1/1       Running   0          3m
```

You can view the persistent volumes, as follows:

```sh
kubectl get persistentvolumes
```

```output
NAME                                       CAPACITY   ACCESS MODES   RECLAIM POLICY   STATUS    CLAIM                          STORAGECLASS   REASON    AGE
pvc-f3301c41-1110-11e8-8231-42010a8a0083   1Gi       RWO            Delete           Bound     default/datadir-yb-master-0    standard                 5m
pvc-f33f29b3-1110-11e8-8231-42010a8a0083   1Gi       RWO            Delete           Bound     default/datadir-yb-master-1    standard                 5m
pvc-f35005b6-1110-11e8-8231-42010a8a0083   1Gi       RWO            Delete           Bound     default/datadir-yb-master-2    standard                 5m
pvc-f36189ab-1110-11e8-8231-42010a8a0083   1Gi       RWO            Delete           Bound     default/datadir-yb-tserver-0   standard                 5m
pvc-f366a4af-1110-11e8-8231-42010a8a0083   1Gi       RWO            Delete           Bound     default/datadir-yb-tserver-1   standard                 5m
pvc-f36d2892-1110-11e8-8231-42010a8a0083   1Gi       RWO            Delete           Bound     default/datadir-yb-tserver-2   standard                 5m
```

You can view all the services by running the following command:

```sh
kubectl get services
```

```output
NAME          TYPE        CLUSTER-IP   EXTERNAL-IP   PORT(S)                               AGE
kubernetes    ClusterIP   XX.XX.XX.X   <none>        443/TCP                               23m
yb-masters    ClusterIP   None         <none>        7000/TCP,7100/TCP                     17m
yb-tservers   ClusterIP   None         <none>        9000/TCP,9100/TCP,9042/TCP,6379/TCP   14m
```

## Connect to the cluster

You can connect to the YCQL API by running the following:

```sh
kubectl exec -it yb-tserver-0 -- ycqlsh yb-tserver-0
```

```output
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
ycqlsh> DESCRIBE KEYSPACES;

system_schema  system_auth  system
```

## Destroy cluster

Destroy the YugabyteDB cluster you created above by running the following:

```sh
kubectl delete -f https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/cloud/kubernetes/yugabyte-statefulset.yaml
```

```output
service "yb-masters" deleted
statefulset "yb-master" deleted
service "yb-tservers" deleted
statefulset "yb-tserver" deleted
```

To destroy the persistent volume claims and lose all the data, run the following:

```sh
kubectl delete pvc -l app=yb-master
kubectl delete pvc -l app=yb-tserver
```

## Destroy the GKE cluster

To destroy the machines you created for the `gcloud` cluster, run the following command:

```sh
gcloud container clusters delete yugabyte
```

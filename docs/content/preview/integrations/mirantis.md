---
title: Mirantis Kubernetes Engine
linkTitle: Mirantis MKE
description: Mirantis
menu:
  preview_integrations:
    identifier: mirantis
    parent: integrations-other
    weight: 571
type: docs
---

[Mirantis Kubernetes Engine (MKE)](https://docs.mirantis.com/mke/3.5/overview.html) is a container orchestration platform for developing and running modern applications at scale, on private clouds, public clouds, and on bare metal.

MKE as a container orchestration platform is especially beneficial in the following scenarios:

- Orchestrating more than one container
- Robust and scalable applications deployment
- Multi-tenant software offerings

The following sections describe how to deploy a single-node YugabyteDB cluster on Mirantis MKE using [kubectl](https://kubernetes.io/docs/reference/kubectl/) and [helm](https://helm.sh/).

This page describes the steps for a single-node cluster for the purpose of simplicity, as you require more than one machine/VM for a multi-node cluster deployment.

## Prerequisite

Before installing a single-node YugabyteDB cluster, ensure that you have the Docker runtime installed on the host on which you are installing MKE. To download and install Docker, select one of the following environments:

<i class="fa-brands fa-apple" aria-hidden="true"></i> [Docker for Mac](https://store.docker.com/editions/community/docker-ce-desktop-mac)

<i class="fa-brands fa-centos"></i> [Docker for CentOS](https://store.docker.com/editions/community/docker-ce-server-centos)

<i class="fa-brands fa-ubuntu"></i> [Docker for Ubuntu](https://store.docker.com/editions/community/docker-ce-server-ubuntu)

<i class="icon-debian"></i> [Docker for Debian](https://store.docker.com/editions/community/docker-ce-server-debian)

## Install and configure Mirantis

1. Install MKE docker image as follows:

    ```sh
    docker image pull mirantis/ucp:3.5.8
    ```

    ```sh
    docker container run --rm -it --name ucp \
          -v /var/run/docker.sock:/var/run/docker.sock \
          mirantis/ucp:3.5.8 install \
          --host-address <node-ip> \ // Replace <node-ip> with the IP address of your machine.
          --interactive
    ```

    When prompted, enter the `username` and `password` that you want to set; these are used to access the MKE web UI.

1. Install and configure kubectl with MKE, and install Helm using the instructions in [MKE documentation](https://docs.mirantis.com/mke/3.5/ops/access-cluster/configure-kubectl.html?highlight=kubectl).

1. Create a new storage class using the following steps:

    1. ​​Copy the following content to a file named `storage.yaml`:

        ```conf
        apiVersion: storage.k8s.io/v1
        kind: StorageClass
        metadata:
         name: yb-storage
        provisioner: kubernetes.io/no-provisioner
        volumeBindingMode: WaitForFirstConsumer
        ```

    1. Apply the configuration using the following command:

        ```sh
        kubectl apply -f storage.yaml
        ```

1. Make the new storage class default as follows:

    ```sh
    kubectl patch storageclass yb-storage -p '{"metadata": {"annotations":{"storageclass.kubernetes.io/is-default-class":"true"}}}'
    ```

1. Verify that the storage class is created using the following command:

    ```sh
    kubectl get storageclass
    ```

    ```output
    NAME                 PROVISIONER                   RECLAIMPOLICY   VOLUMEBINDINGMODE      ALLOWVOLUMEEXPANSION   AGE
    yb-storage(default)  kubernetes.io/no-provisioner  Delete          WaitForFirstConsumer   false                  23s
    ```

1. Create four [PersistentVolumes](https://kubernetes.io/docs/concepts/storage/persistent-volumes/)(PVs) using the following steps:

    1. Copy the following PersistentVolume configuration to a file `volume.yaml`:

        ```conf
        apiVersion: v1
        kind: PersistentVolume
        metadata:
          name: task-pv-volume1
          labels:
            type: local
        spec:
          storageClassName: yb-storage
          capacity:
            storage: 10Gi
          accessModes:
             - ReadWriteOnce
          hostPath:
            path: "/mnt/data"
        ```

    1. Apply the configuration using the following command:

        ```sh
        kubectl apply -f volume.yaml
        ```

    1. Repeat the preceding two steps to create the remaining PVs : `task-pv-volume2`, `task-pv-volume3`, and `task-pv-volume4` by changing the metadata name in `volume.yaml` for each volume and re-running the `kubectl apply` command on the same file.

1. Verify PersistentVolumes are created using the following command:

    ```sh
    kubectl get pv
    ```

    ```output
    NAME             CAPACITY   ACCESS MODES    RECLAIM POLICY    STATUS     CLAIM   STORAGECLASS  REASON  AGE
    task-pv-volume1  10Gi       RWO             Retain            Available          yb-storage            103s
    task-pv-volume2  10Gi       RWO             Retain            Available          yb-storage            82s
    task-pv-volume3  10Gi       RWO             Retain            Available          yb-storage            70s
    task-pv-volume4  10Gi       RWO             Retain            Available          yb-storage            60s
    ```

## Deploy YugabyteDB

### Download YugabyteDB Helm chart

To download and start YugabyteDB Helm chart, perform the following:

1. Add the charts repository using the following command:

    ```sh
    helm repo add yugabytedb https://charts.yugabyte.com
    ```

1. Fetch updates from the repository using the following command:

    ```sh
    helm repo update
    ```

1. Validate the chart version as follows:

    ```sh
    helm search repo yugabytedb/yugabyte --version 2.17.2
    ```

    ```output
    NAME                 CHART VERSION   APP VERSION    DESCRIPTION
    yugabytedb/yugabyte  2.17.2          2.17.2.0-b216  YugabyteDB is the high-performance distributed ...
    ```

### Create a cluster

Create a single-node YugabyteDB cluster using the following command:

```sh
kubectl create namespace yb-demo
helm install yb-demo yugabytedb/yugabyte \
    --version 2.17.2 \
    --set resource.master.requests.cpu=0.5,resource.master.requests.memory=0.5Gi,\
    resource.tserver.requests.cpu=0.5,resource.tserver.requests.memory=0.5Gi,\
    replicas.master=1,replicas.tserver=1 --namespace yb-demo
```

### Check cluster status with `kubectl`

Run the following command to verify that you have two services with one running pod in each: one YB-Master pod (yb-master-0) and one YB-Tserver pod (yb-tserver-0):

```sh
kubectl --namespace yb-demo get pods
```

```output
NAME          READY     STATUS             RESTARTS   AGE
yb-master-0   0/2       ContainerCreating  0          5s
yb-tserver-0  0/2       ContainerCreating  0          4s
```

For details on the roles of these pods in a YugabyteDB cluster, refer to [Architecture](../../architecture/).

The status of all the pods change to  _Running_ state in a few seconds, as per the following output:

```output
NAME          READY     STATUS    RESTARTS   AGE
yb-master-0   2/2       Running   0          13s
yb-tserver-0  2/2       Running   0          12s
```

## Connect to the database

Connect to your cluster using [ysqlsh](../../admin/ysqlsh/), and interact with it using distributed SQL. ysqlsh is installed with YugabyteDB and is located in the bin directory of the YugabyteDB home directory.

1. To start ysqlsh with kubectl, run the following command:

    ```sh
    kubectl --namespace yb-demo exec -it yb-tserver-0 -- sh -c "cd /home/yugabyte && ysqlsh -h yb-tserver-0 --echo-queries"
    ```

    ```output
    ysqlsh (11.2-YB-{{<yb-version version="preview">}}-b0)
    Type "help" for help.

    yugabyte=#
    ```

1. To load sample data and explore an example using ysqlsh, refer to [Retail Analytics](../../sample-data/retail-analytics/).

## Access the MKE web UI

To control your cluster visually with MKE , refer to [Access the MKE web UI](https://docs.mirantis.com/mke/3.6/ops/access-cluster/access-web-ui.html).

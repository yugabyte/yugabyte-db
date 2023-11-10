---
title: Deploy on Azure Kubernetes Service (AKS) using StatefulSet YAML
headerTitle: Azure Kubernetes Service (AKS)
linkTitle: Azure Kubernetes Service (AKS)
description: Use StatefulSet YAML to deploy a single-zone Kubernetes cluster on Azure Kubernetes Service (AKS).
menu:
  v2.18:
    parent: deploy-kubernetes-sz
    name: Azure Kubernetes Service
    identifier: k8s-aks-2
    weight: 624
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
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      StatefulSet YAML
    </a>
  </li>
</ul>


## Prerequisites

Before deploying YugabyteDB on AKS, perform the following:

- Connect to the Azure Cloud Shell. See [Azure bash cloud shell](https://shell.azure.com/bash).

- Register the necessary Azure service providers by running the following:

  ```sh
  az provider register -n Microsoft.Network
  az provider register -n Microsoft.Storage
  az provider register -n Microsoft.Compute
  az provider register -n Microsoft.ContainerService
  ```

- Execute the following command to configure a default location. Remember to replace `eastus` with an appropriate Azure location (region) of your choice that supports AKS clusters:

  ```sh
  az configure --defaults location=eastus
  ```

## Create an Azure cluster

Create an Azure resource group, a logical group in which Azure resources are deployed and managed.

Execute the following command to specify a default location or pass the location parameter to create the resource:

```sh
az group create --name yb-eastus-resource
```

 The resources you create for the AKS cluster will live in this Azure resource.

Create a three-node AKS cluster by running the following command:

```sh
az aks create --resource-group yb-eastus-resource --name yb-aks-cluster --node-count 3 --generate-ssh-keys
```

Configure `kubectl` to work with this cluster, as follows:

```sh
az aks get-credentials --resource-group yb-eastus-resource --name yb-aks-cluster
```

Verify the cluster by running the following command:

```sh
kubectl get nodes
```

```output
NAME                       STATUS    ROLES     AGE       VERSION
aks-nodepool1-25019584-0   Ready     agent     4h        v1.7.9
aks-nodepool1-25019584-1   Ready     agent     4h        v1.7.9
aks-nodepool1-25019584-2   Ready     agent     4h        v1.7.9
```

## Create a YugabyteDB cluster

Create a YugabyteDB cluster by running the following command:

```sh
curl -s "https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/cloud/kubernetes/yugabyte-statefulset.yaml" | sed "s/storageClassName: standard/storageClassName: default/g" | kubectl create -f -
```

```output
service "yb-masters" created
statefulset "yb-master" created
service "yb-tservers" created
statefulset "yb-tserver" created
```

## Check the cluster

Check which pods are running using the following command:

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

View the persistent volumes, as follows:

```sh
kubectl get persistentvolumes
```

```output
NAME                                       CAPACITY   ACCESS MODES   RECLAIM POLICY   STATUS    CLAIM                          STORAGECLASS   REASON    AGE
pvc-849395f7-36f2-11e8-9445-0a58ac1f27f1   1Gi        RWO            Delete           Bound     default/datadir-yb-master-0    default                  12m
pvc-8495d8cd-36f2-11e8-9445-0a58ac1f27f1   1Gi        RWO            Delete           Bound     default/datadir-yb-master-1    default                  12m
pvc-8498b836-36f2-11e8-9445-0a58ac1f27f1   1Gi        RWO            Delete           Bound     default/datadir-yb-master-2    default                  12m
pvc-84abba1a-36f2-11e8-9445-0a58ac1f27f1   1Gi        RWO            Delete           Bound     default/datadir-yb-tserver-0   default                  12m
pvc-84af3484-36f2-11e8-9445-0a58ac1f27f1   1Gi        RWO            Delete           Bound     default/datadir-yb-tserver-1   default                  12m
pvc-84b35d19-36f2-11e8-9445-0a58ac1f27f1   1Gi        RWO            Delete           Bound     default/datadir-yb-tserver-2   default                  12m
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

To open the YCQL shell (`ycqlsh`), run the following command:

```sh
kubectl exec -it yb-tserver-0 -- ycqlsh yb-tserver-0
```

```output
Connected to local cluster at 127.0.0.1:9042.
[ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
ycqlsh> DESCRIBE KEYSPACES;

system_schema  system_auth  system
```

## Destroy the YugabyteDB cluster

You can destroy the YugabyteDB cluster by running the following command:

```sh
kubectl delete -f https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/cloud/kubernetes/yugabyte-statefulset.yaml
```

```output
service "yb-masters" deleted
statefulset "yb-master" deleted
service "yb-tservers" deleted
statefulset "yb-tserver" deleted
```

To destroy the persistent volume claims (and lose all the data), run the following commands:

```sh
$ kubectl delete pvc -l app=yb-master
$ kubectl delete pvc -l app=yb-tserver
```

## Destroy the AKS cluster

To destroy the resource you created for the AKS cluster, run the following:

```sh
az group delete --name yb-eastus-resource
```

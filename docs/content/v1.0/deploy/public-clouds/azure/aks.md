## Prerequisites

- Connect to the Azure Cloud Shell. You can connect and get a shell from the browser by navigating to [Azure bash cloud shell](https://shell.azure.com/bash).

- Register the necessary Azure service providers by running the following:

```{.sh .copy}
az provider register -n Microsoft.Network
az provider register -n Microsoft.Storage
az provider register -n Microsoft.Compute
az provider register -n Microsoft.ContainerService
```

- Configure a default location. Remember to replace `eastus` with an appropriate Azure location (region) of your choice that supports AKS clusters.

```{.sh .copy}
az configure --defaults location=eastus
```


## 1. Create an Azure cluster

- Create an Azure resource

An Azure resource group is a logical group in which Azure resources are deployed and managed. You need to specify a default location or pass the location parameter to create the resource. The resources we create for the AKS cluster will live in this Azure resouce.
<div class='copy separator-dollar'>
```sh
$ az group create --name yb-eastus-resource
```
</div>

- Create the AKS cluster.

You can create a three node AKS cluster by running the following command.
<div class='copy separator-dollar'>
```sh
$ az aks create --resource-group yb-eastus-resource --name yb-aks-cluster --node-count 3 --generate-ssh-keys
```
</div>

Configure `kubectl` to work with this cluster.
<div class='copy separator-dollar'>
```sh
$ az aks get-credentials --resource-group yb-eastus-resource --name yb-aks-cluster
```
</div>

Verify the cluster by running the following command.
<div class='copy separator-dollar'>
```sh
$ kubectl get nodes
```
</div>
```
NAME                       STATUS    ROLES     AGE       VERSION
aks-nodepool1-25019584-0   Ready     agent     4h        v1.7.9
aks-nodepool1-25019584-1   Ready     agent     4h        v1.7.9
aks-nodepool1-25019584-2   Ready     agent     4h        v1.7.9
```

## 2. Create a YugaByte DB cluster

Create a YugaByte DB cluster by running the following.
<div class='copy separator-dollar'>
```sh
$ kubectl create -f https://raw.githubusercontent.com/YugaByte/yugabyte-db/master/cloud/kubernetes/yugabyte-statefulset.yaml
```
</div>
```sh
service "yb-masters" created
statefulset "yb-master" created
service "yb-tservers" created
statefulset "yb-tserver" created
```

## 3. Check the cluster

You should see the following pods running.
<div class='copy separator-dollar'>
```sh
$ kubectl get pods
```
</div>
```sh
NAME           READY     STATUS    RESTARTS   AGE
yb-master-0    1/1       Running   0          3m
yb-master-1    1/1       Running   0          3m
yb-master-2    1/1       Running   0          3m
yb-tserver-0   1/1       Running   0          3m
yb-tserver-1   1/1       Running   0          3m
yb-tserver-2   1/1       Running   0          3m
```

You can view the persistent volumes.
<div class='copy separator-dollar'>
```sh
kubectl get persistentvolumes
```
</div>
```
NAME                                       CAPACITY   ACCESS MODES   RECLAIM POLICY   STATUS    CLAIM                          STORAGECLASS   REASON    AGE
pvc-849395f7-36f2-11e8-9445-0a58ac1f27f1   1Gi        RWO            Delete           Bound     default/datadir-yb-master-0    default                  12m
pvc-8495d8cd-36f2-11e8-9445-0a58ac1f27f1   1Gi        RWO            Delete           Bound     default/datadir-yb-master-1    default                  12m
pvc-8498b836-36f2-11e8-9445-0a58ac1f27f1   1Gi        RWO            Delete           Bound     default/datadir-yb-master-2    default                  12m
pvc-84abba1a-36f2-11e8-9445-0a58ac1f27f1   1Gi        RWO            Delete           Bound     default/datadir-yb-tserver-0   default                  12m
pvc-84af3484-36f2-11e8-9445-0a58ac1f27f1   1Gi        RWO            Delete           Bound     default/datadir-yb-tserver-1   default                  12m
pvc-84b35d19-36f2-11e8-9445-0a58ac1f27f1   1Gi        RWO            Delete           Bound     default/datadir-yb-tserver-2   default                  12m
```

You can view all the services by running the following command.
<div class='copy separator-dollar'>
```sh
$ kubectl get services
```
</div>
```sh
NAME          TYPE        CLUSTER-IP   EXTERNAL-IP   PORT(S)                               AGE
kubernetes    ClusterIP   XX.XX.XX.X   <none>        443/TCP                               23m
yb-masters    ClusterIP   None         <none>        7000/TCP,7100/TCP                     17m
yb-tservers   ClusterIP   None         <none>        9000/TCP,9100/TCP,9042/TCP,6379/TCP   14m
```

## 4. Connect to the cluster

You can connect to the YCQL API by running the following.
<div class='copy separator-dollar'>
```sh
$ kubectl exec -it yb-tserver-0 bin/cqlsh
```
</div>
```
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh> DESCRIBE KEYSPACES;

system_schema  system_auth  system
```


## 5. Destroy the YugaByte DB cluster (optional)

Destroy the YugaByte DB cluster we created above by running the following.
<div class='copy separator-dollar'>
```sh
$ kubectl delete -f https://raw.githubusercontent.com/YugaByte/yugabyte-db/master/cloud/kubernetes/yugabyte-statefulset.yaml
```
</div>
```sh
service "yb-masters" deleted
statefulset "yb-master" deleted
service "yb-tservers" deleted
statefulset "yb-tserver" deleted
```

To destroy the persistent volume claims (**you will lose all the data if you do this**), run:

```{.sh .copy}
kubectl delete pvc -l app=yb-master
kubectl delete pvc -l app=yb-tserver
```

## 6. Destroy the AKS cluster (optional)

To destroy the resource we created for the AKS cluster, run the following.
<div class='copy separator-dollar'>
```sh
$ az group delete --name yb-eastus-resource
```
</div>

## Advanced Kubernetes Deployment

More advanced scenarios for deploying in Kubernetes are covered in the [Kubernetes Deployments](../../../deploy/kubernetes/) section.

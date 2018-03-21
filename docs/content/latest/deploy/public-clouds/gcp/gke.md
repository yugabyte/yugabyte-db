## Prerequisites

- Download and install the [Google Cloud SDK](https://cloud.google.com/sdk/downloads/).

**NOTE:** If you install gcloud using a package manager (as opposed to downloading and installing it manually), it does not support some of the commands below.

- After installing Cloud SDK, install the kubectl command-line tool by running the following command:
```{.sh .copy .separator-dollar}
$ gcloud components install kubectl
```

- Configure defaults for gcloud

Set the project id as `yugabyte`. You can change this as per your need.
```{.sh .copy .separator-dollar}
$ gcloud config set project yugabyte
```

Set the defaut compute zone as `us-west1-b`. You can change this as per your need.
```{.sh .copy .separator-dollar}
gcloud config set compute/zone us-west1-b
```


## 1. Create a cluster

Create a Kubernetes cluster if you have not already done so by running the following command.

```{.sh .copy .separator-dollar}
$ gcloud container clusters create yugabyte
```

Create a YugaByte DB cluster by running the following.

```{.sh .copy .separator-dollar}
$ kubectl create -f https://downloads.yugabyte.com/kubernetes/yugabyte-statefulset.yaml
```
```sh
service "yb-masters" created
statefulset "yb-master" created
service "yb-tservers" created
statefulset "yb-tserver" created
```

You should see the following pods running.

```{.sh .copy .separator-dollar}
$ kubectl get pods
```
```sh
NAME           READY     STATUS    RESTARTS   AGE
yb-master-0    1/1       Running   0          3m
yb-master-1    1/1       Running   0          3m
yb-master-2    1/1       Running   0          3m
yb-tserver-0   1/1       Running   0          3m
yb-tserver-1   1/1       Running   0          3m
yb-tserver-2   1/1       Running   0          3m
```

```{.sh .copy .separator-dollar}
$ kubectl get persistentvolumes
```
```sh
NAME                                       CAPACITY   ACCESS MODES   RECLAIM POLICY   STATUS    CLAIM                          STORAGECLASS   REASON    AGE
pvc-f3301c41-1110-11e8-8231-42010a8a0083   1Gi       RWO            Delete           Bound     default/datadir-yb-master-0    standard                 5m
pvc-f33f29b3-1110-11e8-8231-42010a8a0083   1Gi       RWO            Delete           Bound     default/datadir-yb-master-1    standard                 5m
pvc-f35005b6-1110-11e8-8231-42010a8a0083   1Gi       RWO            Delete           Bound     default/datadir-yb-master-2    standard                 5m
pvc-f36189ab-1110-11e8-8231-42010a8a0083   1Gi       RWO            Delete           Bound     default/datadir-yb-tserver-0   standard                 5m
pvc-f366a4af-1110-11e8-8231-42010a8a0083   1Gi       RWO            Delete           Bound     default/datadir-yb-tserver-1   standard                 5m
pvc-f36d2892-1110-11e8-8231-42010a8a0083   1Gi       RWO            Delete           Bound     default/datadir-yb-tserver-2   standard                 5m
```

```{.sh .copy .separator-dollar}
$ kubectl get services
```
```sh
NAME          TYPE        CLUSTER-IP   EXTERNAL-IP   PORT(S)                               AGE
kubernetes    ClusterIP   10.7.240.1   <none>        443/TCP                               23m
yb-masters    ClusterIP   None         <none>        7000/TCP,7100/TCP                     17m
yb-tservers   ClusterIP   None         <none>        9000/TCP,9100/TCP,9042/TCP,6379/TCP   14m
```


## 2. Destroy cluster (optional)

```{.sh .copy .separator-dollar}
$ kubectl delete -f yugabyte-statefulset.yaml
```
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
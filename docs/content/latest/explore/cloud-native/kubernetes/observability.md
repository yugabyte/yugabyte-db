## 1. Setup - create universe

If you have a previously running local universe, destroy it using the following.

```{.sh .copy .separator-dollar}
$ kubectl delete -f yugabyte-statefulset.yaml
```

Start a new local cluster - by default, this will create a 3 node universe with a replication factor of 3.

```{.sh .copy .separator-dollar}
$ kubectl apply -f yugabyte-statefulset.yaml
```

## Step 6. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.

```{.sh .copy .separator-dollar}
$ kubectl delete -f yugabyte-statefulset.yaml
```

Further, to destroy the persistent volume claims (**you will lose all the data if you do this**), run:

```{.sh .copy}
kubectl delete pvc -l app=yb-master
kubectl delete pvc -l app=yb-tserver
```

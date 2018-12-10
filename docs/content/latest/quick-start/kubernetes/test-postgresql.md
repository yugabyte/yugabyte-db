## Destroy any existing cluster.

```{.sh .copy .separator-dollar}
$ kubectl delete -f yugabyte-statefulset.yaml
```

## Create new cluster 
Create a new cluster with PostgreSQL API enabled by uncommenting the following flags in the YAML file and recreating the cluster. 

```{.sh .copy .separator-dollar}
         # To support postgres functionality, uncomment the following flags.
         #  - "--start_pgsql_proxy"
         #  - "--pgsql_proxy_bind_address=$(POD_IP):5433"
```

```{.sh .copy .separator-dollar}
$ kubectl apply -f yugabyte-statefulset.yaml
```

## Check cluster status

Run the command below to see that we now have two services with 3 pods each - 3 `yb-master` pods (yb-master-1,yb-master-2,yb-master-3) and 3 `yb-tserver` pods (yb-tserver-1,yb-tserver-2,yb-tserver-3) running. Roles played by these pods in a YugaByte DB cluster (aka Universe) is explained in detail [here](../../architecture/concepts/universe/).

```{.sh .copy .separator-dollar}
$ kubectl get pods
```
```sh
NAME           READY     STATUS              RESTARTS   AGE
yb-master-0    0/1       ContainerCreating   0          5s
yb-master-1    0/1       ContainerCreating   0          5s
yb-master-2    1/1       Running             0          5s
yb-tserver-0   0/1       ContainerCreating   0          4s
yb-tserver-1   0/1       ContainerCreating   0          4s
yb-tserver-2   0/1       ContainerCreating   0          4s
```

Eventually all the pods will have the `Running` state.

```{.sh .copy .separator-dollar}
$ kubectl get pods
```
```sh
NAME           READY     STATUS    RESTARTS   AGE
yb-master-0    1/1       Running   0          13s
yb-master-1    1/1       Running   0          13s
yb-master-2    1/1       Running   0          13s
yb-tserver-0   1/1       Running   1          12s
yb-tserver-1   1/1       Running   1          12s
yb-tserver-2   1/1       Running   1          12s
```

## Initialize the PostgreSQL API
```{.sh .copy .separator-dollar}
$ kubectl exec -it yb-tserver-0 bash --  -c "YB_ENABLED_IN_POSTGRES=1 FLAGS_pggate_master_addresses=yb-master-0.yb-masters.default.svc.cluster.local:7100,yb-master-1.yb-masters.default.svc.cluster.local:7100,yb-master-2.yb-masters.default.svc.cluster.local:7100 /home/yugabyte/postgres/bin/initdb -D /tmp/yb_pg_initdb_tmp_data_dir -U postgres"
```

## Run psql to connect to the service.

```{.sh .copy .separator-dollar}
$ kubectl exec -it yb-tserver-0 /home/yugabyte/postgres/bin/psql -- -U postgres -d postgres -h yb-tserver-0 -p 5433
```

```sh
psql (10.4)
Type "help" for help.

postgres=#
```

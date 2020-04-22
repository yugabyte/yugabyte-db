First copy the schema and data files into one of the containers.

```sh
$ kubectl --namespace yb-demo cp ./schema.sql yb-tserver-0:/home/yugabyte/.
```

Create a data directory to copy the data files.

```sh
$ kubectl --namespace yb-demo exec -it yb-tserver-0 bash
```

```sh
$ mkdir data
```

Exit out of the container after the above command.

Now copy the data files.

```sh
$ kubectl --namespace yb-demo cp ./data/orders.sql yb-tserver-0:/home/yugabyte/data/.
kubectl --namespace yb-demo cp ./data/products.sql yb-tserver-0:/home/yugabyte/data/.
kubectl --namespace yb-demo cp ./data/reviews.sql yb-tserver-0:/home/yugabyte/data/.
kubectl --namespace yb-demo cp ./data/users.sql yb-tserver-0:/home/yugabyte/data/.
```

Run ysqlsh to connect to the service.

```sh
$ kubectl --namespace yb-demo exec -it yb-tserver-0  -- /home/yugabyte/bin/ysqlsh -h yb-tserver-0  --echo-queries
```

```
ysqlsh (11.2-YB-2.0.0.0-b0)
Type "help" for help.

yugabyte=#
```

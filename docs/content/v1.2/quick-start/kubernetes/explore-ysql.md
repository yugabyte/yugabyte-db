First copy the schema and data files into one of the containers.
```sh
$ kubectl cp ./schema.sql yb-tserver-n1:/home/yugabyte/.
```

Create a data directory to copy the data files.
```sh
$ kubectl exec -it yb-tserver-n1 bash
```
```sh
$ mkdir data
```
Exit out of the container after the above command.

Now copy the data files.
```sh
$ kubectl cp ./data/orders.sql yb-tserver-n1:/home/yugabyte/data/.
kubectl cp ./data/products.sql yb-tserver-n1:/home/yugabyte/data/.
kubectl cp ./data/reviews.sql yb-tserver-n1:/home/yugabyte/data/.
kubectl cp ./data/users.sql yb-tserver-n1:/home/yugabyte/data/.
```

Run ysqlsh to connect to the service.

```sh
$ kubectl exec -it yb-tserver-0 -- /home/yugabyte/bin/ysqlsh -h yb-tserver-0 --echo-queries
```

```
ysqlsh (11.2)
Type "help" for help.

postgres=#
```

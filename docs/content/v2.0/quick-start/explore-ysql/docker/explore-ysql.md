
First copy the schema and data files into one of the containers.

```sh
$ docker cp ./schema.sql yb-tserver-n1:/home/yugabyte/.
```

To copy the data files, create a `data` directory.

```sh
$ docker exec -it yb-tserver-n1 bash
```

```sh
$ mkdir data
```

Exit out of the container after the above command.

Now, copy the data files.

```sh
$ docker cp ./data/orders.sql yb-tserver-n1:/home/yugabyte/data/.
docker cp ./data/products.sql yb-tserver-n1:/home/yugabyte/data/.
docker cp ./data/reviews.sql yb-tserver-n1:/home/yugabyte/data/.
docker cp ./data/users.sql yb-tserver-n1:/home/yugabyte/data/.
```

To connect to the service, run `ysqlsh`.

```sh
$ docker exec -it yb-tserver-n1 /home/yugabyte/bin/ysqlsh -h yb-tserver-n1  --echo-queries
```

```
ysqlsh (11.2-YB-2.0.0.0-b0)
Type "help" for help.

yugabyte=#
```

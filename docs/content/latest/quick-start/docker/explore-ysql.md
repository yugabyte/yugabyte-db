---
---

First copy the schema and data files into one of the containers.
```sh
$ docker cp ./schema.sql yb-tserver-n1:/home/yugabyte/.
```

Create a data directory to copy the data files.
```sh
$ docker exec -it yb-tserver-n1 bash
```
```sh
$ mkdir data
```
Exit out of the container after the above command.

Now copy the data files.
```sh
$ docker cp ./data/orders.sql yb-tserver-n1:/home/yugabyte/data/.
docker cp ./data/products.sql yb-tserver-n1:/home/yugabyte/data/.
docker cp ./data/reviews.sql yb-tserver-n1:/home/yugabyte/data/.
docker cp ./data/users.sql yb-tserver-n1:/home/yugabyte/data/.
```

Run ysqlsh to connect to the service.

```sh
$ docker exec -it yb-tserver-n1 /home/yugabyte/bin/ysqlsh -h yb-tserver-n1 -p 5433 -U postgres  --echo-queries
```

```
ysqlsh (11.2)
Type "help" for help.

postgres=#
```

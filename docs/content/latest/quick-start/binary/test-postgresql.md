## 1. Create a new cluster
- Destroy any existing cluster.

```sh
$ ./bin/yb-ctl destroy
```

- Create a new cluster with YSQL API enabled. Note the additional option `enable_postgres` passed to the create cluster command.

You can do this as shown below.
```sh
$ ./bin/yb-ctl create --enable_postgres
```


- Check status of the cluster and confirm that the special `postgres` node is now running. This is the node we will connect postgres clients to. Internally, this node will use the various tserver nodes to distribute the data across the entire cluster.

```sh
$ ./bin/yb-ctl status
```

```
2019-01-15 22:18:40,387 INFO: Server is running: type=master, node_id=1, PID=12818, admin service=http://127.0.0.1:7000
2019-01-15 22:18:40,394 INFO: Server is running: type=master, node_id=2, PID=12821, admin service=http://127.0.0.2:7000
2019-01-15 22:18:40,401 INFO: Server is running: type=master, node_id=3, PID=12824, admin service=http://127.0.0.3:7000
2019-01-15 22:18:40,408 INFO: Server is running: type=tserver, node_id=1, PID=12827, admin service=http://127.0.0.1:9000, cql service=127.0.0.1:9042, redis service=127.0.0.1:6379, pgsql service=127.0.0.1:5433
2019-01-15 22:18:40,415 INFO: Server is running: type=tserver, node_id=2, PID=12830, admin service=http://127.0.0.2:9000, cql service=127.0.0.2:9042, redis service=127.0.0.2:6379, pgsql service=127.0.0.2:5433
2019-01-15 22:18:40,422 INFO: Server is running: type=tserver, node_id=3, PID=12833, admin service=http://127.0.0.3:9000, cql service=127.0.0.3:9042, redis service=127.0.0.3:6379, pgsql service=127.0.0.3:5433
```

- Run psql to connect to the service.

You can do this as shown below.

```sh
$ ./bin/psql -h 127.0.0.1 -p 5433 -U postgres
```

```
psql (10.3, server 10.4)
Type "help" for help.

postgres=#
```

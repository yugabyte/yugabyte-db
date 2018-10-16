- Destroy any existing cluster.

```{.sh .copy .separator-dollar}
$ ./bin/yb-ctl destroy
```

- Create a new cluster with PostgreSQL API enabled. Note the additional option `enable_postgres` passed to the create cluster command.

```{.sh .copy .separator-dollar}
$ ./bin/yb-ctl create --enable_postgres
```

- Check status of the cluster and confirm that the special `postgres` node is now running. This is the node we will connect postgres clients to. Internally, this node will use the various tserver nodes to distribute the data across the entire cluster.

```{.sh .copy .separator-dollar}
$ ./bin/yb-ctl status
```
```sh
2018-10-15 13:12:33,129 INFO: Server is running: type=master, node_id=1, PID=3076, admin service=http://127.0.0.1:7000
2018-10-15 13:12:33,151 INFO: Server is running: type=master, node_id=2, PID=3079, admin service=http://127.0.0.2:7000
2018-10-15 13:12:33,174 INFO: Server is running: type=master, node_id=3, PID=3082, admin service=http://127.0.0.3:7000
2018-10-15 13:12:33,200 INFO: Server is running: type=tserver, node_id=1, PID=3085, admin service=http://127.0.0.1:9000, cql service=127.0.0.1:9042, redis service=127.0.0.1:6379
2018-10-15 13:12:33,223 INFO: Server is running: type=tserver, node_id=2, PID=3088, admin service=http://127.0.0.2:9000, cql service=127.0.0.2:9042, redis service=127.0.0.2:6379
2018-10-15 13:12:33,246 INFO: Server is running: type=tserver, node_id=3, PID=3091, admin service=http://127.0.0.3:9000, cql service=127.0.0.3:9042, redis service=127.0.0.3:6379
2018-10-15 13:12:33,270 INFO: Server is running: type=postgres, node_id=1, PID=3107, pgsql service=127.0.0.1:5433
```

- Run psql to connect to the service.

```{.sh .copy .separator-dollar}
$ ./bin/psql -p 5433 -U postgres
```
```sh
Database 'username' does not exist
psql (10.3, server 10.4)
Type "help" for help.

postgres=#
```


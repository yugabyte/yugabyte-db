- Destroy any existing cluster.

```{.sh .copy .separator-dollar}
$ ./yb-docker-ctl destroy
```

- Create a new cluster with PostgreSQL API enabled. Note the additional option `enable_postgres` passed to the create cluster command. Also note that this requires at least version `1.1.2.0-b10` of YugaByte DB.

```{.sh .copy .separator-dollar}
$ ./yb-docker-ctl create --enable_postgres
```

- Check status of the cluster and confirm that the special `postgres` pod is now running. This is the pod we will connect postgres clients to. Internally, this node will use the various tserver nodes to distribute the data across the entire cluster.

```{.sh .copy .separator-dollar}
$ ./yb-docker-ctl status
```
```sh
ID             PID        Type       Node                 URL                       Status          Started At
90116e4d86d6   6056       postgres   yb-postgres-n1       http://192.168.64.8:9000  Running         2018-10-18T22:02:53.127652078Z
ca16705b20bd   5861       tserver    yb-tserver-n3        http://192.168.64.7:9000  Running         2018-10-18T22:02:52.12697026Z
0a7deab4e4db   5681       tserver    yb-tserver-n2        http://192.168.64.6:9000  Running         2018-10-18T22:02:51.181289786Z
921494a8058d   5547       tserver    yb-tserver-n1        http://192.168.64.5:9000  Running         2018-10-18T22:02:50.187976253Z
0d7dc9436033   5345       master     yb-master-n3         http://192.168.64.4:7000  Running         2018-10-18T22:02:49.105792573Z
0b25dd24aea3   5191       master     yb-master-n2         http://192.168.64.3:7000  Running         2018-10-18T22:02:48.162506832Z
feea0823209a   5039       master     yb-master-n1         http://192.168.64.2:7000  Running         2018-10-18T22:02:47.163244578Z
```

- Run psql to connect to the service.

```{.sh .copy .separator-dollar}
$ docker exec -it yb-postgres-n1 /home/yugabyte/postgres/bin/psql -p 5433 -U postgres
```

```sh
psql (10.4)
Type "help" for help.

postgres=#
```

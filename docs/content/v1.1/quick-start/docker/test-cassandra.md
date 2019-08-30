

* Run cqlsh to connect to the service.

You can do this as shown below.

```sh
$ docker exec -it yb-tserver-n3 /home/yugabyte/bin/cqlsh
```

```
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh> 
```

* Run a cql command to verify it is working.

```sql
cqlsh> describe keyspaces;
```

```
system_schema  system_auth  system

cqlsh> 
```

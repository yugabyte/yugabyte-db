
* Run cqlsh to connect to the service.

```sh
$ kubectl exec -it yb-tserver-0 /home/yugabyte/bin/ycqlsh yb-tserver-0
```

```
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
ycqlsh>
```

* Run a cql command to verify it is working.

```sql
ycqlsh> describe keyspaces;
```

```
system_schema  system_auth  system

ycqlsh> 
```

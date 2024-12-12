<!--
+++
private = true
+++
-->

To connect to the service, open the YCQL shell (`ycqlsh`) by running the following command:

```sh
$ kubectl exec -it yb-tserver-0 -- ycqlsh yb-tserver-0
```

```output
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
ycqlsh>
```

Run a cql command to verify it is working.

```cql
ycqlsh> describe keyspaces;
```

```output
system_schema  system_auth  system

ycqlsh> 
```

## 1. Setup - create universe

If you have a previously running local universe, destroy it using the following.

```sh
$ ./yb-docker-ctl destroy
```

Start a new local universe with replication factor 5. This will create 5 nodes by default.

```sh
$ ./yb-docker-ctl create --rf 5 
```

Add 2 more nodes.

```sh
$ ./yb-docker-ctl add_node
```

```sh
$ ./yb-docker-ctl add_node
```

## 2. Write some data

By default, the key-value sample application runs with strong read consistency where all data is read from the tablet leader. We are going to populate exactly one key with a 10KB value into the system. Since the replication factor is 5, this key will get replicated to 5 of the 7 nodes in the universe.

Let us run the CQL sample key-value app to constantly update this key-value, as well as perform reads with strong consistency against the local universe.

```sh
$ docker cp yb-master-n1:/home/yugabyte/java/yb-sample-apps.jar .
```

```sh
$ java -jar ./yb-sample-apps.jar --workload CassandraKeyValue \
                                    --nodes localhost:9042 \
                                    --nouuid \
                                    --num_unique_keys 1 \
                                    --num_threads_write 1 \
                                    --num_threads_read 1 \
                                    --value_size 10240
```


In the above command, we have set the value of `num_unique_keys` to 1, which means we are overwriting a single key `key:0`. We can verify this using cqlsh:

```sh
$ docker exec -it yb-tserver-n1 /home/yugabyte/bin/cqlsh
```

```
Connected to local cluster at localhost:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
```

```sql
cqlsh> SELECT k FROM ybdemo_keyspace.cassandrakeyvalue;
```

```
 k
-------
 key:0

(1 rows)
```

## 3. Strongly consistent reads from tablet leaders

When performing strongly consistent reads as a part of the above command, all reads will be served by the tablet leader of the tablet that contains the key `key:0`. If we browse to the <a href='http://localhost:7000/tablet-servers' target="_blank">tablet-servers</a> page, we will see that all the requests are indeed being served by one tserver:

![Reads from the tablet leader](/images/ce/tunable-reads-leader-docker.png)


## 4. Timeline consistent reads from tablet replicas

Let us stop the above sample app, and run the following variant of the sample app. This command will do updates to the same key `key:0` which will go through the tablet leader, but it will reads from the replicas.

```sh
$ java -jar ./yb-sample-apps.jar --workload CassandraKeyValue \
                                    --nodes localhost:9042 \
                                    --nouuid \
                                    --num_unique_keys 1 \
                                    --num_threads_write 1 \
                                    --num_threads_read 1 \
                                    --value_size 10240 \
                                    --local_reads
```

This can be easily seen by refreshing the <a href='http://localhost:7000/tablet-servers' target="_blank">tablet-servers</a> page, where we will see that the writes are served by a single TServer that is the leader of the tablet for the key `key:0` while multiple TServers which are replicas serve the reads.

![Reads from the tablet follower](/images/ce/tunable-reads-followers-docker.png)


## 5. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.

```sh
$ ./yb-docker-ctl destroy
```

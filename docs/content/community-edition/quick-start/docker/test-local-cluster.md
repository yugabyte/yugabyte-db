## Test CQL service

[**cqlsh**](http://cassandra.apache.org/doc/latest/tools/cqlsh.html) is a command line shell for interacting with Apache Cassandra through [CQL (the Cassandra Query Language)](http://cassandra.apache.org/doc/latest/cql/index.html). It utilizes the Python CQL driver, and connects to the single node specified on the command line. For ease of use, the YugaByte DB container ships with the 3.10 version of cqlsh in its bin directory.

### Connect with cqlsh

- Run cqlsh

```sh
$ docker exec -it yb-tserver-n3 /home/yugabyte/bin/cqlsh
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh> 
```

- Run a cql command

```sql
cqlsh> describe keyspaces;

system_schema  system_auth  system  default_keyspace

cqlsh> 
```

### Run a CQL sample app

- Verify that Java is installed on your localhost.

```sh
$ java -version
```

- Run the CQL time-series sample app using the executable jar

```sh
# copy the java sample apps jar from one of the containers to the localhost
$ docker cp yb-master-n1:/home/yugabyte/yb-sample-apps.jar .

# run the jar
$ java -jar ./yb-sample-apps.jar --workload CassandraTimeseries --nodes localhost:9042
```

As you can see above, the CQL time-series sample app first creates a keyspace `ybdemo_keyspace` and a table `ts_metrics_raw`. It then starts multiple writer and reader threads to generate the load. The read/write ops count and latency metrics observed should not be used for performance testing purposes.

- Verify using cqlsh

```sql
# connect to yb-tserver-n3 to observe the data
$ docker exec -it yb-tserver-n3 /home/yugabyte/bin/cqlsh
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh> use ybdemo_keyspace;
cqlsh:ybdemo_keyspace> describe tables;

ts_metrics_raw

cqlsh:ybdemo_keyspace> describe ybdemo_keyspace.ts_metrics_raw;

CREATE TABLE ybdemo_keyspace.ts_metrics_raw (
    user_id text,
    metric_id text,
    node_id text,
    ts timestamp,
    value text,
    PRIMARY KEY ((user_id, metric_id), node_id, ts)
) WITH CLUSTERING ORDER BY (node_id ASC, ts ASC)
    AND default_time_to_live = 86400;

cqlsh:ybdemo_keyspace> select * from ybdemo_keyspace.ts_metrics_raw limit 10;

 user_id                                 | metric_id                 | node_id    | ts                              | value
-----------------------------------------+---------------------------+------------+---------------------------------+--------------------------
 66-ac8c8289-ce19-41dc-b1f9-4ed78a1fa4da | metric-00007.yugabyte.com | node-00001 | 2017-10-20 18:29:40.000000+0000 | 1508524180000[B@1a728121
 66-ac8c8289-ce19-41dc-b1f9-4ed78a1fa4da | metric-00007.yugabyte.com | node-00001 | 2017-10-20 18:29:41.000000+0000 | 1508524181000[B@5d0a04c9
 66-ac8c8289-ce19-41dc-b1f9-4ed78a1fa4da | metric-00007.yugabyte.com | node-00001 | 2017-10-20 18:29:42.000000+0000 | 1508524182000[B@765dfd86
 66-ac8c8289-ce19-41dc-b1f9-4ed78a1fa4da | metric-00007.yugabyte.com | node-00001 | 2017-10-20 18:29:43.000000+0000 | 1508524183000[B@27a60876
 66-ac8c8289-ce19-41dc-b1f9-4ed78a1fa4da | metric-00007.yugabyte.com | node-00001 | 2017-10-20 18:29:44.000000+0000 | 1508524184000[B@1102abe3
 66-ac8c8289-ce19-41dc-b1f9-4ed78a1fa4da | metric-00007.yugabyte.com | node-00001 | 2017-10-20 18:29:45.000000+0000 | 1508524185000[B@452ad6ce
 66-ac8c8289-ce19-41dc-b1f9-4ed78a1fa4da | metric-00007.yugabyte.com | node-00001 | 2017-10-20 18:29:46.000000+0000 | 1508524186000[B@27078325
 66-ac8c8289-ce19-41dc-b1f9-4ed78a1fa4da | metric-00007.yugabyte.com | node-00001 | 2017-10-20 18:29:47.000000+0000 | 1508524187000[B@34f28dea
 66-ac8c8289-ce19-41dc-b1f9-4ed78a1fa4da | metric-00007.yugabyte.com | node-00001 | 2017-10-20 18:29:48.000000+0000 | 1508524188000[B@78f5473d
 66-ac8c8289-ce19-41dc-b1f9-4ed78a1fa4da | metric-00007.yugabyte.com | node-00001 | 2017-10-20 18:29:49.000000+0000 | 1508524189000[B@3b9792f9

(10 rows)
cqlsh:ybdemo_keyspace>

```

## Test Redis service 

### Connect with redis-cli

[redis-cli](https://redis.io/topics/rediscli) is a command line interface to interact with a Redis server. For ease of use, the YugaByte DB package ships with the 4.0.1 version of redis-cli in its bin directory.

```
$ docker exec -it yb-tserver-n3 /home/yugabyte/bin/redis-cli
127.0.0.1:6379> set mykey somevalue
OK
127.0.0.1:6379> get mykey
"somevalue"
```

### Run a Redis sample app

- Verify that Java is installed on your localhost.

```sh
$ java -version
```

- Run the Redis key-value sample app using the executable jar

```sh
# copy the java sample apps jar from one of the containers to the localhost (if you haven't done so already)
$ docker cp yb-master-n1:/home/yugabyte/yb-sample-apps.jar .

# run the redis sample app
$ java -jar ./yb-sample-apps.jar --workload RedisKeyValue --nodes localhost:6379 --nouuid
```

- Verify with redis-cli

```sh
$ ./bin/redis-cli
127.0.0.1:6379> get key:1  
"val:1"  
127.0.0.1:6379> get key:2  
"val:2"  
127.0.0.1:6379> get key:100  
"val:100"  
127.0.0.1:6379>   
```

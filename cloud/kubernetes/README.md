# YugaByte on Kubernetes StatefulSets

This page has details on deploying YugaByte DB on [Kubernetes](https://kubernetes.io) using the `StatefulSets` feature. [StatefulSets](https://kubernetes.io/docs/concepts/workloads/controllers/statefulset/) can be used to manage a deployment and scale of a set of Pods. It provides guarantees about the ordering and uniqueness of these Pods.

## Requirements

### Version of kubernetes: 1.8 or later
Since we will be using the StatefulSets feature of Kubernetes, you need a version that supports that feature. This tutorial has been tested on the 1.8 version.
You can install kubernetes by following [these instructions](https://kubernetes.io/docs/tasks/tools/install-minikube/).
You can check the version of kubernetes installed using the following command:
```
$ kubectl version
Client Version: version.Info{Major:"1", Minor:"9", GitVersion:"v1.9.1", ...}
Server Version: version.Info{Major:"1", Minor:"8", GitVersion:"v1.8.0", ...}
```

## Creating a local cluster

To create a 3-node local cluster with replication factor 3, run the following command.
```
$ kubectl apply -f yugabyte-statefulset.yaml
service "yb-masters" created
statefulset "master" created
service "yb-tservers" created
statefulset "tserver" created
```

Make sure the pods are all in the running state.
```
$ kubectl get pods
NAME           READY     STATUS    RESTARTS   AGE
yb-master-0    1/1       Running   0          13s
yb-master-1    1/1       Running   0          13s
yb-master-2    1/1       Running   0          13s
yb-tserver-0   1/1       Running   1          12s
yb-tserver-1   1/1       Running   1          12s
yb-tserver-2   1/1       Running   1          12s
```

### Using Redis API

The Cassandra query layer is automatically initialized. The Redis API has to be initialized by creating a Redis table.
You can do this as follows
```
kubectl exec -it yb-master-0 /home/yugabyte/bin/yb-admin -- --master-addresses yb-master-0.yb-masters.default.svc.cluster.local:7100,yb-master-1.yb-masters.default.svc.cluster.local:7100,yb-master-2.yb-masters.default.svc.cluster.local:7100 setup_redis_table
...
I0127 19:38:10.358551   115 client.cc:1292] Created table system_redis.redis of type REDIS_TABLE_TYPE
I0127 19:38:10.358872   115 yb-admin_client.cc:400] Table 'system_redis.redis' created.
```
You can connect to the Redis API of the YugaByte DB cluster running on kubernetes using `redis-cli` as follows.
```
$ kubectl exec -it yb-tserver-0 /home/yugabyte/bin/redis-cli
127.0.0.1:6379> 
127.0.0.1:6379> PING
"PONG"
```

### Using the Cassandra API

You can connect to the Cassandra API of the YugaByte DB cluster running on kubernetes using `cqlsh` as follows.
```
$ kubectl exec -it yb-tserver-0 /home/yugabyte/bin/cqlsh
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh> 
cqlsh> describe keyspaces;

system_schema  system_auth  system
```

## Read More

You can see all the above and more details on our website docs:
- [Quick start](https://docs.yugabyte.com/quick-start/install/)
- [Explore core features](https://docs.yugabyte.com/quick-start/install/)

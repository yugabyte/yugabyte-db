---
title: Connect Remote Clients to Kubernetes Clusters
headerTitle: Connect Clients to Kubernetes Clusters
linkTitle: Connect Clients
description: Connect remote clients to YugabyteDB clusters deployed within Kubernetes.
menu:
  v2.20:
    identifier: clients-kubernetes
    parent: deploy-kubernetes
    weight: 626
type: docs
---

There is a number of options available for connecting to a YugabyteDB cluster deployed within Kubernetes.

## Prerequisites

You must have a YugabyteDB cluster set up according to the [Kubernetes deployment instructions.](../../kubernetes/)

## Connect from within the Kubernetes cluster

An application that is deployed within the Kubernetes cluster should use the Service DNS name `yb-tservers.<namespace>.svc.cluster.local` to discover server endpoints. This DNS entry has multiple `A` records, one for each YB-TServer pod, so that clients can randomize queries across different endpoints:

```sh
kubectl --namespace yb-demo get svc/yb-tservers
```

```output
NAME          TYPE        CLUSTER-IP   EXTERNAL-IP   PORT(S)                                        AGE
yb-tservers   ClusterIP   None         <none>        7100/TCP,9000/TCP,6379/TCP,9042/TCP,5433/TCP   56m
```

The following example shows a client that uses the YSQL shell ([ysqlsh](../../../admin/ysqlsh/)) to connect:

```sh
kubectl run ysqlsh-client -it --rm  --image yugabytedb/yugabyte-client --command -- ysqlsh -h yb-tservers.yb-demo.svc.cluster.local
```

```sql
yugabyte=# CREATE TABLE demo(id INT PRIMARY KEY);
```

```output
CREATE TABLE
```

The following example shows a client that uses the YCQL shell ([ycqlsh](../../../admin/ycqlsh/)) to connect:

```sh
kubectl run cqlsh-shell -it --rm  --image yugabytedb/yugabyte-client --command -- cqlsh yb-tservers.yb-demo.svc.cluster.local 9042
```

```CQL
ycqlsh> CREATE KEYSPACE demo;
ycqlsh> use demo;
ycqlsh:demo> CREATE TABLE t_demo(id INT PRIMARY KEY);
```

Note that although tables are [internally sharded](../../../architecture/concepts/yb-tserver/) across multiple YB-TServer pods, every YB-TServer pod has the ability to process any query, irrespective of its actual tablet assignment.

## Connect externally

An application that is deployed outside the Kubernetes cluster should use the external LoadBalancer IP address to connect to the cluster. Connections to the load balancer IP address are randomly routed to one of the YB-TServer pods behind the YB-TServer service:

```sh
kubectl get svc -n yb-demo
```

```output
NAME                 TYPE           CLUSTER-IP      EXTERNAL-IP      PORT(S)                                       AGE
yb-master-ui         LoadBalancer   10.101.142.48   98.138.219.231   7000:32168/TCP                                43h
yb-masters           ClusterIP      None            <none>           7100/TCP,7000/TCP                             43h
yb-tserver-service   LoadBalancer   10.99.76.181    98.138.219.232   6379:30141/TCP,9042:31059/TCP,5433:30577/TCP  43h
yb-tservers          ClusterIP      None            <none>           7100/TCP,9000/TCP,6379/TCP,9042/TCP,5433/TCP  43h
```

The following example shows a client that uses the YSQL shell ([ysqlsh](../../../admin/ysqlsh/)) to connect:

```sh
docker run yugabytedb/yugabyte-client ysqlsh -h 98.138.219.232
```

```sql
yugabyte=# CREATE TABLE demo(id INT PRIMARY KEY);
```

```output
CREATE TABLE
```

The following example shows a client that uses the YCQL shell ([ycqlsh](../../../admin/ycqlsh/)) to connect:

```sh
docker run yugabytedb/yugabyte-client ycqlsh 98.138.219.232 9042
```

```CQL
ycqlsh> CREATE KEYSPACE demo;
ycqlsh> use demo;
ycqlsh:demo> CREATE TABLE t_demo(id INT PRIMARY KEY);
```

## Use YB-Master Admin UI

The YB-Master Admin UI is available at the IP address exposed by the `yb-master-ui` LoadBalancer service at `https://98.138.219.231:7000/`.

Another option that does not require an external LoadBalancer is to create a tunnel from the local host to the master web server port on the master pod using [kubectl port-forward](https://kubernetes.io/docs/tasks/access-application-cluster/port-forward-access-application-cluster/), as follows:

```sh
kubectl port-forward pod/yb-master-0 7000:7000 -n yb-demo
```

```output
Forwarding from 127.0.0.1:7000 -> 7000
Forwarding from [::1]:7000 -> 7000
```

## Connect externally to a Minikube cluster

When the Kubernetes cluster is set up using [Minikube](https://kubernetes.io/docs/setup/learning-environment/minikube/), an external IP address is not available by default for the LoadBalancer endpoints. To enable the load balancer IP address, run the following command `minikube tunnel`:

```sh
minikube tunnel
```

```output
Status:
    machine: minikube
    pid: 38193
    route: 10.96.0.0/12 -> 192.168.99.100
    minikube: Running
    services: [yb-master-ui, yb-tserver-service]
    errors:
        minikube: no errors
        router: no errors
        loadbalancer emulator: no errors
```

For details, see [LoadBalancer access](https://minikube.sigs.k8s.io/docs/handbook/accessing/#loadbalancer-access).

## Connect TLS-secured YugabyteDB cluster deployed by Helm charts

To start a YugabyteDB cluster with encryption in transit (TLS) enabled, follow the steps at [Google Kubernetes Service (GKE) - Helm Chart](/preview/deploy/kubernetes/single-zone/gke/helm-chart/) and set the flag `tls.enabled=true` in the helm command line, as shown in the following example:

```shell
helm install yugabyte --namespace yb-demo --name yb-demo --set=tls.enabled=true
```

### Connect from within the Kubernetes cluster

Copy the following `yb-client.yaml` and use the `kubectl create -f yb-client.yaml` command to create a pod with auto-mounted client certificates:

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: yb-client
  namespace: yb-demo
spec:
  containers:
  - name: yb-client
    image: yugabytedb/yugabyte-client:latest
    env:
    - name: SSL_CERTFILE
      value: "/root/.yugabytedb/root.crt"
    volumeMounts:
    - name: yugabyte-tls-client-cert
      mountPath: "/root/.yugabytedb/"
  volumes:
  - name: yugabyte-tls-client-cert
    secret:
      secretName: yugabyte-tls-client-cert
      defaultMode: 256
```

When a client uses the `YSQL shell` ([`ysqlsh`](../../../admin/ysqlsh/)) to connect, you can execute the following command to verify the connection:

```sh
kubectl exec -n yb-demo -it yb-client -- ysqlsh -h yb-tservers.yb-demo.svc.cluster.local "sslmode=require"
```

```output
ysqlsh (11.2-YB-2.1.5.0-b0)
SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
Type "help" for help.
```

```sql
yugabyte=# \conninfo
```

```output
You are connected to database "yugabyte" as user "yugabyte" on host "yb-tservers.yb-demo.svc.cluster.local" at port "5433".
SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
```

When a client uses the YCQL shell ([`ycqlsh`](../../../admin/ycqlsh/)) to connect, you can execute the following command to verify the connection:

```sh
kubectl exec -n yb-demo -it yb-client -- ycqlsh yb-tservers.yb-demo.svc.cluster.local 9042 --ssl
```

```output
Connected to local cluster at yb-tservers.yb-demo.svc.cluster.local:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
```

```CQL
cqlsh> SHOW HOST
```

```output
Connected to local cluster at yb-tservers.yb-demo.svc.cluster.local:9042
```

Optionally, you can use the following command to remove the client pod after the operations have been completed:

```sh
kubectl delete pod yb-client -n yb-demo
```

```output
pod "yb-client" deleted
```

### Connect externally

To connect externally to a TLS-enabled YugabyteDB helm cluster, start by downloading the root certificate from the Kubernetes cluster's  secrets, as follows:

```sh
mkdir $(pwd)/certs
kubectl get secret yugabyte-tls-client-cert  -n yb-demo -o jsonpath='{.data.root\.crt}' | base64 --decode > $(pwd)/certs/root.crt
```

When a client that uses the `YSQL shell` ([`ysqlsh`](../../../admin/ysqlsh/)) to connect, the command to execute specifies the external LoadBalancer IP of the `yb-tserver-service`, as described in [Connect using external clients](../single-zone/oss/helm-chart/#connect-using-external-clients). You can verify the connection via the following command:

```sh
docker run -it --rm -v $(pwd)/certs/:/root/.yugabytedb/:ro yugabytedb/yugabyte-client:latest ysqlsh -h <External_Cluster_IP> "sslmode=require"
```

```output
ysqlsh (11.2-YB-2.1.5.0-b0)
SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
Type "help" for help.
```

```sh
yugabyte=# \conninfo
```

```output
You are connected to database "yugabyte" as user "yugabyte" on host "35.200.205.208" at port "5433".
SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
```

When a client uses the `YCQL shell` ([`ycqlsh`](../../../admin/ycqlsh/)) to connect, you can verify the connection by executing the following `docker run` command:

```sh
docker run -it --rm -v $(pwd)/certs/:/root/.yugabytedb/:ro \
--env SSL_CERTFILE=/root/.yugabytedb/root.crt yugabytedb/yugabyte-client:latest ycqlsh <External_Cluster_IP> 9042 --ssl
```

```output
ysqlsh (11.2-YB-2.1.5.0-b0)
Connected to local cluster at 35.200.205.208:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
```

```CQL
cqlsh> SHOW HOST
```

```output
Connected to local cluster at 35.200.205.208:9042.
```

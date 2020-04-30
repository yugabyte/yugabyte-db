---
title: Open Source Kubernetes
linkTitle: Open Source Kubernetes
description: Open Source Kubernetes
block_indexing: true
menu:
  v2.0:
    parent: deploy-kubernetes
    name: Open Source
    identifier: k8s-oss-3
    weight: 621
type: page
isTocNested: true
showAsideToc: true
---


<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="/latest/deploy/kubernetes/oss/helm-chart" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Helm chart
    </a>
  </li>
  <li >
    <a href="/latest/deploy/kubernetes/oss/yugabyte-operator" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      YugabyteDB operator
    </a>
  </li>
  <li >
    <a href="/latest/deploy/kubernetes/oss/operator-hub" class="nav-link active">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Operator Hub
    </a>
  </li>
  <li>
    <a href="/latest/deploy/kubernetes/oss/rook-operator" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Rook operator
    </a>
  </li>
</ul>

This is an alternative to deploying YugabyteDB manually using [Yugabyte Operator](yugabyte-operator.md). The Yugabyte operator is available on Red Hat's [OperatorHub.io](https://operatorhub.io/operator/yugabyte-operator) and hence YugabyteDB can be also be deployed using [Operator Lifecycle Manager](https://github.com/operator-framework/operator-lifecycle-manager). As the name suggests, OLM is a tool to help deploy and manage Operators on your cluster.

Read on to find out how you can deploy YugabyteDB with OLM.


## Prerequisites
A Kubernetes cluster and `kubectl` configured to talk to the cluster.

## Deploy YugabyteDB using Operator Lifecycle Manager
YugabyteDB can be deployed to any Kubernetes cluster using OLM in three easy steps.

1. Deploy Operator Lifecycle Manager, so that it can manage Operator deployments for you.
```sh
$ curl -sL https://github.com/operator-framework/operator-lifecycle-manager/releases/download/0.13.0/install.sh | bash -s 0.13.0
```

2. Install YugabyteDB operator
```sh
$ kubectl create -f https://operatorhub.io/install/yugabyte-operator.yaml
```

Watch your operator come up until it reaches `Succeeded` phase.
```sh
$ kubectl get csv -n operators

NAME                       DISPLAY             VERSION   REPLACES   PHASE
yugabyte-operator.v0.0.1   Yugabyte Operator   0.0.1                Succeeded
```

3. Create YugabyteDB Custom Resource to create YugabyteDB cluster using operator deployed above
```sh
$ kubectl create namespace yb-operator && kubectl create -f https://raw.githubusercontent.com/yugabyte/yugabyte-operator/master/deploy/crds/yugabyte_v1alpha1_ybcluster_cr.yaml
```

Watch your YugabteDB cluster pods come up

```sh
$ kubectl get pods -n yb-operator

NAME           READY   STATUS    RESTARTS   AGE
yb-master-0    1/1     Running   0          3m32s
yb-master-1    1/1     Running   0          3m32s
yb-master-2    1/1     Running   0          3m31s
yb-tserver-0   1/1     Running   0          3m31s
yb-tserver-1   1/1     Running   0          3m31s
yb-tserver-2   1/1     Running   0          3m31s
```

## Configuration options

Refer [Yugabyte Operator](../yugabyte-operator/#configuration-options) for configuration options.

## Use YugabyteDB

When all of the pods in YugabyteDB cluster are running, you can use the YSQL shell to access the YSQL API, which is PostgreSQL-compliant.

```console
kubectl exec -it -n yb-operator yb-tserver-0 -- /home/yugabyte/bin/ysqlsh -h yb-tserver-0  --echo-queries
```

For details on the YSQL API, see:

- [Explore YSQL](../../../../quick-start/explore-ysql/#kubernetes)
- [YSQL Reference](../../../../api/ysql/)

## Cleanup

Run the commands below to remove the YugabyteDB cluster and operator resources.

**NOTE:** This will destroy your database and delete all of its data.

```console
kubectl delete -f https://raw.githubusercontent.com/yugabyte/yugabyte-operator/master/deploy/crds/yugabyte_v1alpha1_ybcluster_cr.yaml
kubectl delete namespace yb-operator
kubectl delete -f https://operatorhub.io/install/yugabyte-operator.yaml
```

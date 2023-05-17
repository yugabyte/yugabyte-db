---
title: Deploy on Kubernetes clusters using Operator Hub and OLM
headerTitle: Open source Kubernetes
linkTitle: Open source Kubernetes
description: Deploy YugabyteDB on Kubernetes clusters using Operator Hub and Operator Lifecycle Manager (OLM).
menu:
  v2.16:
    parent: deploy-kubernetes-sz
    name: Open Source
    identifier: k8s-oss-3
    weight: 621
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="{{< relref "./helm-chart.md" >}}" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Helm chart
    </a>
  </li>
  <li >
    <a href="{{< relref "./yugabyte-operator.md" >}}" class="nav-link">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      YugabyteDB operator
    </a>
  </li>
  <li >
    <a href="{{< relref "./operator-hub.md" >}}" class="nav-link active">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      Operator Hub
    </a>
  </li>
</ul>

This is an alternative to deploying YugabyteDB manually using [Yugabyte Operator](../yugabyte-operator/). The Yugabyte operator is available on Red Hat's [OperatorHub.io](https://operatorhub.io/operator/yugabyte-operator) and hence YugabyteDB can be also be deployed using [Operator Lifecycle Manager](https://github.com/operator-framework/operator-lifecycle-manager) (OLM). As the name suggests, OLM is a tool to help deploy and manage operators on your cluster.

## Prerequisites

A Kubernetes cluster and `kubectl` must be configured to communicate with the cluster.

## Deploy YugabyteDB using OLM

YugabyteDB can be deployed on any Kubernetes cluster using OLM, as follows:

1. Deploy OLM to enable it to manage the operator deployments, as follows:

   ```sh
   curl -sL https://github.com/operator-framework/operator-lifecycle-manager/releases/download/0.13.0/install.sh | bash -s 0.13.0
   ```

2. Install YugabyteDB operator, as follows:

   ```sh
   kubectl create -f https://operatorhub.io/install/yugabyte-operator.yaml
   ```

   The operator should reach the `Succeeded` phase:

   ```sh
   kubectl get csv -n operators
   ```

   ```output
   NAME                       DISPLAY             VERSION   REPLACES   PHASE
   yugabyte-operator.v0.0.1   Yugabyte Operator   0.0.1                Succeeded
   ```

3. Create YugabyteDB Custom Resource using the operator deployed in the previous step:

   ```sh
   kubectl create namespace yb-operator && kubectl create -f https://raw.githubusercontent.com/yugabyte/yugabyte-operator/master/deploy/crds/yugabyte.com_v1alpha1_ybcluster_cr.yaml
   ```

   The YugabyteDB cluster pods should be running:

   ```sh
   kubectl get pods -n yb-operator
   ```

   ```output
   NAME           READY   STATUS    RESTARTS   AGE
   yb-master-0    1/1     Running   0          3m32s
   yb-master-1    1/1     Running   0          3m32s
   yb-master-2    1/1     Running   0          3m31s
   yb-tserver-0   1/1     Running   0          3m31s
   yb-tserver-1   1/1     Running   0          3m31s
   yb-tserver-2   1/1     Running   0          3m31s
   ```

## Configuration flags

For information on configuration flags, see [Configuration flags](../yugabyte-operator/#configuration-flags).

## Use YugabyteDB

When all of the pods in the YugabyteDB cluster are running, execute the following command in YSQL shell (`ysqlsh`) to access the YSQL API, which is PostgreSQL-compliant:

```sh
kubectl exec -it -n yb-operator yb-tserver-0 -- ysqlsh -h yb-tserver-0  --echo-queries
```

For details on the YSQL API, see the following:

- [Explore YSQL](../../../../../quick-start/explore/ysql/)
- [YSQL Reference](../../../../../api/ysql/)

## Clean up

To remove the YugabyteDB cluster and operator resources, including the database itself and all the data, run the following commands:

```sh
kubectl delete -f https://raw.githubusercontent.com/yugabyte/yugabyte-operator/master/deploy/crds/yugabyte.com_v1alpha1_ybcluster_cr.yaml
kubectl delete namespace yb-operator
kubectl delete -f https://operatorhub.io/install/yugabyte-operator.yaml
```

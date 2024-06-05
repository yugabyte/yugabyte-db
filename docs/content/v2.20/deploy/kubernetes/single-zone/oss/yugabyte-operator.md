---
title: Open source Kubernetes using YugabyteDB operator
headerTitle: Open source Kubernetes
linkTitle: Open source Kubernetes
description: Deploy a YugabyteDB cluster with a Kubernetes native customer resource.
menu:
  v2.20:
    parent: deploy-kubernetes-sz
    name: Open Source
    identifier: k8s-oss-2
    weight: 621
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../helm-chart/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Helm chart
    </a>
  </li>
  <li >
    <a href="../yugabyte-operator/" class="nav-link active">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      Kubernetes Operator (legacy)
    </a>
  </li>
</ul>

The Kubernetes Operator for YugabyteDB is no longer maintained. It is recommended that you use the actively maintained [Helm charts](../helm-chart/) for YugabyteDB to install YugabyteDB on Kubernetes. For documentation on using the Kubernetes Operator for YugabyteDB, refer to the [GitHub repository](https://github.com/yugabyte/yugabyte-operator).

A newer version of the Kubernetes Operator for YugabyteDB is in development.

<!--
Create and manage a YugabyteDB cluster with a Kubernetes native custom resource `ybcluster.yugabyte.com`.

The custom resource definition and other necessary specifications are available in [YugabyteDB operator repository](https://github.com/yugabyte/yugabyte-operator/). In addition, you may consult the full list of [configuration flags](#configuration-flags).

## Prerequisites

Clone the [yugabyte-operator](https://github.com/yugabyte/yugabyte-operator) repository on your local computer and change directory to the cloned directory.

## Deploy a YugabyteDB cluster with the operator

To create a YugabyteDB cluster, first you need to register the custom resource that would represent YugabyteDB cluster: `ybclusters.yugabyte.com`.

```sh
kubectl create -f deploy/crds/yugabyte.com_ybclusters_crd.yaml
```

Create and set up role-based access control (RBAC) for the operator by running the following command from root of the repository:

```sh
kubectl create -f deploy/operator.yaml
```

After a few seconds the operator should be running. Verify the operator status by running following command:

```sh
kubectl -n yb-operator get po,deployment
```

Finally, create an instance of the custom resource with which the operator would create a YugabyteDB cluster, as follows:

```sh
kubectl create -f deploy/crds/yugabyte.com_v1alpha1_ybcluster_cr.yaml
```

Verify that the cluster is running using the following command:

```sh
kubectl get po,sts,svc
```

You should see three pods each for YB-Master and YB-TServer services.

Once the cluster is running, you may start the YSQL API and start executing relational queries:

```sh
kubectl exec -it yb-tserver-0 -- ysqlsh -h yb-tserver-0 --echo-queries
```

You can also connect to the YCQL API, as follow:

```sh
kubectl exec -it yb-tserver-0 -- ycqlsh yb-tserver-0
```

## Configuration flags

There is a number configuration flags involved.

### Image

Mention YugabyteDB Docker image attributes such as `repository`, `tag` and `pullPolicy` under `image`.

### Replication factor

Specify the required data replication factor. This is a required field.

### TLS

Optionally, enable TLS encryption for YugabyteDB. It is disabled by default. You can enable the TLS encryption with three flags. If you have set `enabled` to `true`, then you need to generate the root certificate and key. Specify them under `rootCA.cert` and `rootCA.key`. Refer to [Create server certificates](../../../../../secure/tls-encryption/server-certificates/) for more information.

### YB-Master and YB-TServer

YB-Master and YB-TServer are two essential components of a YugabyteDB cluster. YB-Master is responsible for recording and maintaining system metadata and for administration activities. YB-TServer is responsible for data input and output.

Specify YB-Master and YB-TServer attributes under `master`/`tserver`. These are required fields.

#### Replicas

Specify count of pods for `master` and `tserver` under the `replicas` field. This is a required field.

#### Network ports

Control network configuration for YB-Master and YB-TServer, each of which only supports attributes of the selected port.

These are optional fields, except `tserver.tserverUIPort`, so there are default values for each port. If any or all of the acceptable field definitions are absent, the default network configuration is used.

A ClusterIP service is created when the `tserver.tserverUIPort` port is specified. If it is not specified, only StatefulSet and headless service are created for YB-TServer; the ClusterIP service creation is skipped. For YB-Master, all three Kubernetes objects are always created.

If `master.enableLoadBalancer` is set to `true`, then master UI service is of type `LoadBalancer`. The YB-TServer UI service is of type `LoadBalancer`, if `tserver.tserverUIPort` is specified and `tserver.enableLoadBalancer` is set to `true`. `tserver.enableLoadBalancer` is ignored if `tserver.tserverUIPort` is not specified.

The following table lists acceptable port names, applicable component (YB-Master or YB-TServer), and port default values:

| Attribute      | Component | Default Value |
| -------------- | --------- | ------------- |
| masterUIPort   | Master    | 7000          |
| masterRPCPort  | Master    | 7100          |
| tserverUIPort  | TServer   | NA            |
| tserverRPCPort | TServer   | 9100          |
| ycqlPort       | TServer   | 9042          |
| yedisPort      | TServer   | 6379          |
| ysqlPort       | TServer   | 5433          |

#### podManagementPolicy

Specify pod management policy for Statefulsets created as part of a YugabyteDB cluster. Valid values are `Parallel` and `OrderedReady`, with `Parallel` being the default value.

#### storage

Specify storage configurations, namely storage `count`, `size`, and `storageClass` of volumes. Typically, one volume per YB-Master instance is sufficient, hence YB-Master has a default storage count of `1`. If the storage class is not specified, it defaults to `standard`. Make sure the Kubernetes admin has defined the`standard` storage class, before leaving this field out.

#### resources

Specify resource `requests` and `limits` under the `resources` attribute. The resources to be specified are `cpu` and `memory`. The `resource` property in itself is optional and is not applied to created `StatefulSets`, if omitted. You may also specify either `resource.requests` or `resource.limits`, or both.

#### flags

Specify flags for additional control of the YugabyteDB cluster. For more information, see [YB-Master flags](../../../../../reference/configuration/yb-master/#flags) and [YB-TServer flags](../../../../../reference/configuration/yb-tserver/#flags).

If you have enabled TLS encryption, then you can set the following flags:

- `use_node_to_node_encryption` flag to enable node-to-node encryption.
- `allow_insecure_connections` flag to specify if insecure connections are allowed when TLS is enabled.
- `use_client_to_server_encryption` flag to enable client-to-node encryption.
-->

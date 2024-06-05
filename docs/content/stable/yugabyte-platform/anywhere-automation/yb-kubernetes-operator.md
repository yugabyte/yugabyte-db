---
title: Yugabyte Kubernetes Operator
headerTitle: Yugabyte Kubernetes Operator
linkTitle: Kubernetes Operator
description: Yugabyte Kubernetes Operator for YugabyteDB Anywhere.
headcontent: Install YugabyteDB Anywhere and create universes using Yugabyte Kubernetes Operator
techPreview: /preview/releases/versioning/#feature-maturity
menu:
  stable_yugabyte-platform:
    parent: anywhere-automation
    identifier: yb-kubernetes-operator
    weight: 100
type: docs
---

The Yugabyte Kubernetes Operator streamlines the deployment and management of YugabyteDB clusters in Kubernetes environments. You can use the Operator to automate provisioning, scaling, and handling lifecycle events of YugabyteDB clusters, and it provides additional capabilities not available via other automation methods (which rely on REST APIs, UIs, and Helm charts).

The Operator establishes `ybuniverse` as a Custom Resource (CR) in Kubernetes, and enables a declarative management of your YugabyteDB Anywhere (YBA) universe. You can update the custom resources to customize the `ybuniverse` resources, including CPU, memory, and disk configurations, and deploy multi-availability zone balanced YBA universes on the underlying cluster for optimal performance. The CR supports seamless upgrades of YBA universes with no downtime, as well as transparent scaling operations.

![Yugabyte Kubernetes operator](/images/yb-platform/yb-kubernetes-operator.png)

## Yugabyte Kubernetes Operator CRDs

The Yugabyte Operator provides additional Custom Resource Definitions (CRDs) to manage the day 2 operations of a YBA universe, including the following:

- Release CRD - run multiple releases of YugabyteDB and upgrade the software in a YBA universe
- Support Bundle CRD - collect logs when a universe fails
- Backup and Restore CRDs - take full backups of a universe and restore for data protection
- Storage Config CRD - configure backup destinations

For details of each CRD, you can run a `kubectl explain` on the CR. For example:

```sh
kubectl explain ybuniverse.spec
```

```output
GROUP:   operator.yugabyte.io
KIND:    YBUniverse
VERSION:  v1alpha1

FIELD: spec <Object>

DESCRIPTION:
  Schema spec for a yugabytedb universe.

FIELDS:
 deviceInfo  <Object>
  Device information for the universe to refer to storage information for
  volume, storage classes etc.

 enableClientToNodeEncrypt   <boolean>
  Enable client to node encryption in the universe. Enable this to use tls
  enabled connnection between client and database.

 enableIPV6  <boolean>
  Enable IPV6 in the universe.

 enableLoadBalancer  <boolean>
  Enable LoadBalancer access to the universe. Creates a service with
  Type:LoadBalancer in the universe for tserver and masters.

 enableNodeToNodeEncrypt    <boolean>
  Enable node to node encryption in the universe. This encrypts the data in
  transit between nodes.

 enableYCQL  <boolean>
  Enable YCQL interface in the universe.

 enableYCQLAuth    <boolean>
  enableYCQLAuth enables authentication for YCQL inteface.

 enableYSQL  <boolean>
  Enable YSQL interface in the universe.

 enableYSQLAuth    <boolean>
  enableYSQLAuth enables authentication for YSQL inteface.

 gFlags    <Object>
  Configuration flags for the universe. These can be set on masters or
  tservers

 kubernetesOverrides  <Object>
  Kubernetes overrides for the universe. Please refer to yugabyteDB
  documentation for more details.
  https://docs.yugabyte.com/preview/yugabyte-platform/create-deployments/create-universe-multi-zone-kubernetes/#configure-helm-overrides

 numNodes   <integer>
  Number of tservers in the universe to create.

 providerName <string>
  Preexisting Provider name to use in the universe.

 replicationFactor   <integer>
  Number of times to replicate data in a universe.

 universeName <string>
  Name of the universe object to create

 ybSoftwareVersion   <string>
  Version of DB software to use in the universe.

 ycqlPassword <Object>
  Used to refer to secrets if enableYCQLAuth is set.

 ysqlPassword <Object>
  Used to refer to secrets if enableYSQLAuth is set.

 zoneFilter  <[]string>
  Only deploy yugabytedb nodes in these zones mentioned in the list. Defaults
  to all zones if unspecified.
```

```sh
kubectl explain ybuniverse.spec.gFlags
```

```output
GROUP:   operator.yugabyte.io
KIND:    YBUniverse
VERSION:  v1alpha1

FIELD: gFlags <Object>

DESCRIPTION:
  Configuration flags for the universe. These can be set on masters or
  tservers

FIELDS:
 masterGFlags <map[string]string>
  Configuration flags for the master process in the universe.

 perAZ <map[string]Object>
  Configuration flags per AZ per process in the universe.

 tserverGFlags <map[string]string>
  Configuration flags for the tserver process in the universe.
```

## Prerequisites

Before installing the Kubernetes Operator and YBA universes, verify that the following components are installed and configured:

- Kubernetes cluster v1.27 or later.
- Helm v3.11 or later.
- Administrative access. Required for the Kubernetes cluster, including ability to create [cluster roles](#cluster-roles-and-namespace-roles), namespaces, and details on setting up necessary roles and permissions for the [service account](#service-account).

### Service account

The Yugabyte Kubernetes Operator requires a service account with sufficient permissions to manage resources in the Kubernetes cluster. When installing the operator, ensure that the service account has the necessary roles and cluster roles bound to it.

### Cluster roles and namespace roles

- `ClusterRole`: grants permissions at the cluster level, necessary for operations that span multiple namespaces, or have cluster-wide implications.
- `Role`: grants permissions in a specific namespace, and used for namespace-specific operations.

The yugaware chart, when installed with `rbac.create=true`, automatically creates appropriate `ClusterRoles` and `Roles` needed for the Kubernetes Operator.

## Installation

For information on installing YBA and creating universes using the Yugabyte Kubernetes Operator, refer to [Use Yugabyte Kubernetes Operator to automate YBA deployments](../../install-yugabyte-platform/install-software/kubernetes/#use-yugabyte-kubernetes-operator-to-automate-yba-deployments).

## Example workflows

### Create a universe

Use the following CRD to create a universe using the `kubectl apply` command:

```sh
kubectl apply universedemo.yaml -n yb-platform
```

```yaml
# universedemo.yaml
apiVersion: operator.yugabyte.io/v1alpha1
kind: YBUniverse
metadata:
  name: operator-universe-demo
spec:
  numNodes:    3
  replicationFactor:  1
  enableYSQL: true
  enableNodeToNodeEncrypt: true
  enableClientToNodeEncrypt: true
  enableLoadBalancer: false
  ybSoftwareVersion: "2.20.1.3-b3"
  enableYSQLAuth: false
  enableYCQL: true
  enableYCQLAuth: false
  gFlags:
    tserverGFlags: {}
    masterGFlags: {}
  deviceInfo:
    volumeSize: 400
    numVolumes: 1
    storageClass: "yb-standard"
  kubernetesOverrides:
    resource:
      master:
        requests:
          cpu: 2
          memory: 8Gi
        limits:
          cpu: 3
          memory: 8Gi
```

Any modifications to the universe can be done by modifying the CRD using `kubectl apply/edit` operations.

### Add a different software release of YugabyteDB

Use the release CRD to add a different software release of YugabyteDB:

```sh
kubectl apply updaterelease.yaml -n yb-platform
```

```yaml
# updaterelease.yaml
apiVersion: operator.yugabyte.io/v1alpha1
kind: Release
metadata:
  name: "2.20.1.3-b3"
spec:
  config:
    version: "2.20.1.3-b3"
    downloadConfig:
      http:
        paths:
          helmChart: "https://charts.yugabyte.com/yugabyte-2.20.1.tgz"
          x86_64: "https://downloads.yugabyte.com/releases/2.20.1.3/yugabyte-2.20.1.3-b3-linux-x86_64.tar.gz"
```

### Backup and restore

Specify a storage configuration CRD to configure backup storage, and perform backup and restore of your YBA universes as per the following example:

```sh
kubectl apply backuprestore.yaml -n yb-platform
```

```yaml
# backuprestore.yaml
apiVersion: operator.yugabyte.io/v1alpha1
kind: StorageConfig
metadata:
  name: s3-config-operator
spec:
  config_type: STORAGE_S3
  data:
    AWS_ACCESS_KEY_ID: <ACCESS_KEY>
    AWS_SECRET_ACCESS_KEY: <SECRET>
    BACKUP_LOCATION: s3://backups.yugabyte.com/s3Backup

apiVersion: operator.yugabyte.io/v1alpha1
kind: Backup
metadata:
  name: operator-backup-1
spec:
  backupType: PGSQL_TABLE_TYPE
  storageConfig: s3-config-operator
  universe: <name-universe>
  timeBeforeDelete: 1234567890
  keyspace: postgres

apiVersion: operator.yugabyte.io/v1alpha1
kind: RestoreJob
metadata:
  name: operator-restore-1
spec:
  actionType: RESTORE
  universe:  <name of universe>
  backup: <name of backup to restore>
  keyspace: <keyspace overide>
```

### Support bundle

Use the following CRD to create a [support bundle](../../troubleshoot/universe-issues/#use-support-bundles):

```sh
kubectl apply supportbundle.yaml -n yb-platform
```

```yaml
# supportbundle.yaml
apiVersion: operator.yugabyte.io/v1alpha1
kind: SupportBundle
metadata:
  name: bundle1
  namespace: user-test
spec:
  universeName: test-1
  collectionTimerange:
    startDate: 2023-08-07T11:55:00Z
  components:
  - UniverseLogs
  - ApplicationLogs
  - OutputFiles
  - ErrorFiles
  - CoreFiles
  - Instance
  - ConsensusMeta
  - TabletMeta
  - YbcLogs
  - K8sInfo
  - GFlags
```

## Limitations

- Yugabyte Kubernetes Operator is in [Tech Preview](/preview/releases/versioning/#feature-maturity), and it is recommended to use the YugabyteDB Helm charts for production deployments.
- Yugabyte Kubernetes Operator can only deploy universes on the _same_ Kubernetes cluster it is deployed on.
- Yugabyte Kubernetes Operator is single cluster only, and does not support multi-cluster universes.
- Currently, Yugabyte Kubernetes Operator does not support the following features:
  - Software upgrade rollback
  - [xCluster](../../../architecture/docdb-replication/async-replication/)
  - [Read Replica](../../../architecture/key-concepts/#read-replica-cluster)
  - [Backup schedules](../../back-up-restore-universes/schedule-data-backups/)
  - [Encryption-At-Rest](../../security/enable-encryption-at-rest/)
- Only self-signed [encryption in transit](../../security/enable-encryption-in-transit/) is supported. Editing this later is not supported.

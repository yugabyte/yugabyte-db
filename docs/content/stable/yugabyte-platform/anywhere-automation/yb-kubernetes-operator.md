---
title: YugabyteDB Kubernetes Operator
headerTitle: YugabyteDB Kubernetes Operator
linkTitle: Kubernetes Operator
description: YugabyteDB Kubernetes Operator for YugabyteDB Anywhere.
headcontent: Install YugabyteDB Anywhere and create universes using YugabyteDB Kubernetes Operator
menu:
  stable_yugabyte-platform:
    parent: anywhere-automation
    identifier: yb-kubernetes-operator
    weight: 100
type: docs
rightNav:
  hideH4: true
---

The YugabyteDB Kubernetes Operator streamlines the deployment and management of YugabyteDB clusters in Kubernetes environments. You can use the Operator to automate provisioning, scaling, and handling lifecycle events of YugabyteDB clusters, and it provides additional capabilities not available via other automation methods (which rely on REST APIs, UIs, and Helm charts).

The Operator establishes `ybuniverse` as a Custom Resource Definition (CRD) in Kubernetes, enabling a declarative management of your YugabyteDB Anywhere (YBA) universe.

You can define and update these custom resources to manage your universe's configuration, including granular resource specifications (CPU and memory for Masters and TServers) and precise regional/zonal placement policies to ensure optimal performance and high availability. Custom resources support seamless upgrades with no downtime, as well as automated, transparent scaling, and cluster-balanced deployments.

You can additionally convert Kubernetes universes that are managed via Helm charts to be managed by the YugabyteDB Kubernetes Operator, using the `operator-import` API. See [Import universe](#import-universe).

![YugabyteDB Kubernetes Operator](/images/yb-platform/yb-kubernetes-operator.png)

## YugabyteDB Kubernetes Operator CRDs

The Operator is built around the **YBUniverse CRD**, which defines and manages a YugabyteDB universe.

The following additional CRDs support day 2 operations.

| CRD | Description |
| :--- | :--- |
| [YBProvider](#create-a-provider) | Define a Kubernetes provider for multi-cluster deployments and operator-managed universes (available in v2025.2.2 or later). |
| [Release](#add-a-different-software-release-of-yugabytedb) | Run multiple releases of YugabyteDB and upgrade the software in a YBA universe. |
| [SupportBundle](#support-bundle) | Collect logs when a universe fails. |
| [StorageConfig](#backup-and-restore) | Configure backup destinations. |
| [Backup and RestoreJob](#backup-and-restore) | Take full backups of a universe and restore for data protection. |
| [BackupSchedule](#scheduled-backups) | Schedule full and incremental backups of a universe. |
| [PitrConfig](#configure-pitr) | Configure point-in-time recovery (PITR) for a universe. |
| [PitrRestore](#restore-from-pitr) | {{<tags/feature/ea idea="2460">}}Restore a universe to a point in time using a PITR configuration. |
| [DrConfig](#configure-xcluster-dr) | {{<tags/feature/ea idea="2460">}}Create and manage [xCluster DR](../../back-up-restore-universes/disaster-recovery/) configurations. |
| [YBCertificate](#configure-tls-certificates) | Configure TLS certificates for encryption in transit (self-signed or cert-manager). |

For details of each CRD, run `kubectl explain` on the CR.

For example, to view all available configuration options for the `YBUniverse` custom resource, run the following command:

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
  Device information for the tservers in universe to refer to storage
  information for volume, storage classes etc. DEPRECATED: Use tserverVolume
  instead. deviceInfo and tserverVolume are mutually exclusive.

 enableClientToNodeEncrypt   <boolean>
  Enable client to node encryption in the universe. Enable this to use tls
  enabled connnection between client and database.

 enableIPV6  <boolean>
  Enable IPV6 in the universe.

 enableLoadBalancer  <boolean>
  Enable LoadBalancer access to the universe. Creates a service with
  Type:LoadBalancer in the universe for tserver and masters. WARNING:
  Enabling LoadBalancer may expose universe servers via public IPs. Use with
  caution. To use an internal LoadBalancer, ensure you set appropriate
  Kubernetes overrides to avoid exposing nodes publicly.

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

 masterDeviceInfo   <Object>
  Device information for the masters in universe to refer to storage
  information for volume, storage classes etc. DEPRECATED: Use masterVolume
  instead. masterDeviceInfo and masterVolume are mutually exclusive.

 masterResourceSpec <Object>
  Resource specification for the master pods in the universe.

 masterVolume   <Object>
  Volume configuration for masters in the universe. This replaces
  masterDeviceInfo. masterDeviceInfo and masterVolume are mutually exclusive.

 masterWaitSeconds  <integer>
  Time in seconds to wait after restarting a Master node before proceeding
  to the next node during a rolling upgrade.

 numNodes   <integer>
  Number of tservers in the universe to create.

 paused  <boolean>
  If the universe is paused. A paused universe will have its statefulsets
  scaled to 0 pods. When unpaused, the statefulsets will be scaled back to
  their previous values. While Paused, all other actions on the universe will
  be ignored until the universe is resumed.

 placementInfo  <Object>
  Placement information for the universe.

 providerName <string>
  Preexisting Provider name to use in the universe.

 readReplica <Object>
  Read replica configuration for the universe.

 replicationFactor   <integer>
  Number of times to replicate data in a universe.

 rollMaxBatchSize   <Object>
  Maximum number of nodes to roll (restart) at a time during a rolling
  upgrade. Only honored when upgradeOption is "Rolling". When unset, nodes
  are rolled one at a time.

 rootCA  <string>
  Specify the name of the rootCA certificate to be used for cert-manager or
  cert-manager certificates. If empty, YBA will create its own certificate.

 tserverResourceSpec  <Object>
  Resource specification for the tserver pods in the universe.

 tserverVolume  <Object>
  Volume configuration for tservers in the universe. This replaces
  deviceInfo. deviceInfo and tserverVolume are mutually exclusive.

 tserverWaitSeconds   <integer>
  Time in seconds to wait after restarting a TServer node before proceeding
  to the next node during a rolling upgrade.

 universeName <string>
  Name of the universe object to create

 upgradeOption  <string>
  Strategy to use when performing upgrade operations (software upgrade,
  GFlags upgrade, Kubernetes overrides upgrade, certificate rotation and YBC
  toggle). "Rolling" upgrades nodes one at a time, "Non-Rolling" upgrades
  all nodes simultaneously with a restart, and "Non-Restart" applies the
  change without restarting nodes. Defaults to "Rolling".

 useYbdbInbuiltYbc  <boolean>
  Use YBDB inbuilt YBC (immutable YBC) for the universe. When true, YBC runs
  from the same container image as the database instead of being installed at
  runtime.

 ybSoftwareVersion   <string>
  Version of DB software to use in the universe.

 ybcThrottleParameters  <Object>
  YBC throttle parameters for the universe. These throttle parameters can be
  used to control speed and resource usage of taking and restoring backups.

 ycqlPassword <Object>
  Used to refer to secrets if enableYCQLAuth is set.

 ysqlPassword <Object>
  Used to refer to secrets if enableYSQLAuth is set.

 zoneFilter  <[]string>
  Filter the zones to be added by the auto-provider. Only used when
  providerName is not specified.
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

Before installing the Kubernetes Operator, verify that the following components are installed and configured:

- Kubernetes cluster v1.27 or later.
- Helm v3.11 or later.
- Administrative access. Required for the Kubernetes cluster, including ability to create [cluster roles](#cluster-roles-and-namespace-roles), namespaces, and details on setting up necessary roles and permissions for the [service account](#service-account).

### Service account

The YugabyteDB Kubernetes Operator requires a service account with sufficient permissions to manage resources in the Kubernetes cluster. When installing the operator, ensure that the service account has the necessary roles and cluster roles bound to it.

### Cluster roles and namespace roles

- `ClusterRole`: grants permissions at the cluster level, necessary for operations that span multiple namespaces, or have cluster-wide implications.
- `Role`: grants permissions in a specific namespace, and used for namespace-specific operations.

The yugaware chart, when installed with `rbac.create=true`, automatically creates appropriate `ClusterRoles` and `Roles` needed for the Kubernetes Operator.

## Install Kubernetes Operator

To use the Kubernetes Operator with YugabyteDB Anywhere, you can either install YugabyteDB Anywhere using the operator, or upgrade an existing YugabyteDB installation.
{{< tabpane text=true >}}

 {{% tab header="New Installation" %}}

To install YugabyteDB Anywhere using the YugabyteDB Kubernetes Operator, do the following:

1. Apply the following CRD:

    ```sh
    kubectl apply -f https://raw.github.com/yugabyte/charts/{{< yb-version version="stable" format="short">}}/crds/concatenated_crd.yaml
    ```

1. Run the following `helm install` command to set the parameters from the preceding YAML file to install the YugabyteDB Anywhere (`yugaware`) Helm chart:

    ```sh
    # Modify the fields kubernetesOperatorNamespace and defaultUser username, email and password fields as required
    helm install yba yugabytedb/yugaware \
      --version {{< yb-version version="stable" format="short">}} \
      --namespace yb-platform \
      --set yugaware.kubernetesOperatorEnabled=true \
      --set yugaware.kubernetesOperatorNamespace='yb-platform-test' \
      --set yugaware.defaultUser.enabled=true \
      --set yugaware.defaultUser.username=yb_platform_user \
      --set yugaware.defaultUser.email='yugabyte_k8s@yugabyte.com' \
      --set yugaware.defaultUser.password='Password#Test123'
    ```

1. Verify that YBA is up, and the Kubernetes Operator is installed successfully using the following commands:

    ```sh
    kubectl get pods -n <yba_namespace>
    ```

 {{% /tab %}}

 {{% tab header="Upgrade Installation" %}}

<span id="existing-yba-installs"></span>

To use the YugabyteDB Kubernetes Operator with an existing YugabyteDB Anywhere instance, perform an upgrade as follows:

1. Apply the following CRD:

    ```sh
    kubectl apply -f https://raw.github.com/yugabyte/charts/{{< yb-version version="stable" format="short">}}/crds/concatenated_crd.yaml
    ```

1. Get a list of Helm chart releases in namespace using the following command:

    ```sh
    helm ls
    ```

    ```output
    NAME    NAMESPACE       REVISION        UPDATED                                 STATUS          CHART           APP VERSION
    yba         yb-platform-test      1               2024-05-08 16:42:47.480260572 +0000 UTC deployed        yugaware-2.19.3 2.19.3.0-b140
    ```

1. Run the following `helm upgrade` command to enable the YBA upgrade:

    ```sh
    helm upgrade yba yugabytedb/yugaware --version {{< yb-version version="stable" format="short">}} \
      --set yugaware.kubernetesOperatorEnabled=true,yugaware.kubernetesOperatorNamespace=yb-platform-test
    ```

1. Verify that YBA is up, and the Kubernetes Operator is installed successfully using the following commands:

    ```sh
    kubectl get pods -n <yba_namespace>
    ```

    ```sh
    kubectl get pods -n <operator_namespace>
    ```

    ```output
    NAME                                       READY   STATUS    RESTARTS   AGE
    chart-1706728534-yugabyte-k8s-operator-0   3/3     Running   0          26h
    ```

    Additionally, you should see no stack traces, but the following messages in the `KubernetesOperatorReconciler` log:

    ```output
    LOG.info("Finished running ybUniverseController");
    ```

 {{% /tab %}}

{{< /tabpane >}}

### Operator High Availability

{{<tags/feature/ea idea="2460">}}If you deploy YBA across separate Kubernetes clusters with [YBA High Availability](../../administer-yugabyte-platform/high-availability/) enabled, Operator HA synchronizes operator CRs and their associated secrets to the standby cluster during failover and failback. This lets the standby YBA instance resume management of operator-controlled universes without manually recreating resources.

For details, see [Operator High Availability](../../administer-yugabyte-platform/operator-high-availability/).

## Example workflows

### Create a provider

Use the YBProvider CRD (available in v2025.2.2 or later) to define a Kubernetes provider that universes can reference via `spec.providerName`. The provider specifies cloud type, image registry, and per-region/per-zone settings such as storage class and namespace.

```sh
kubectl apply provider-demo.yaml -n yb-platform
```

```yaml
# provider-demo.yaml
apiVersion: operator.yugabyte.io/v1alpha1
kind: YBProvider
metadata:
  name: test-provider
spec:
  cloudInfo:
    kubernetesProvider: gke
    kubernetesImageRegistry: quay.io/yugabyte/yugabyte
  regions:
    - code: us-west1
      zones:
        - code: us-west1-a
          cloudInfo:
            kubernetesStorageClass: yb-standard
            kubeNamespace: anabaria-devspace
        - code: us-west1-b
          cloudInfo:
            kubernetesStorageClass: yb-standard
            kubeNamespace: anabaria-devspace
        - code: us-west1-c
          cloudInfo:
            kubernetesStorageClass: yb-standard
            kubeNamespace: anabaria-devspace
```

You can then reference this provider in a [Universe CR with placement information](#create-a-universe-with-placement-information) by setting `spec.providerName` to the provider's `metadata.name` (for example, `test-provider`).

#### Using a custom kubeconfig

To use a custom kubeconfig for the provider, specify it in either top-level `spec.cloudInfo` or in zone-level `cloudInfo`. The kubeconfig content must be stored in a Kubernetes secret with the key `kubeconfig`.

1. Create the secret:

    ```sh
    kubectl create secret generic test-kubeconfig -n yb-operator --from-file=kubeconfig=/tmp/kubeconfig.conf
    ```

1. Reference the secret in the YBProvider manifest via `kubeConfigSecret`:

    ```yaml
    apiVersion: operator.yugabyte.io/v1alpha1
    kind: YBProvider
    metadata:
      name: test-provider
    spec:
      cloudInfo:
        kubernetesProvider: gke
        kubernetesImageRegistry: quay.io/yugabyte/yugabyte
        kubeConfigSecret:
          name: test-kubeconfig
          namespace: yb-operator
      regions:
        - code: us-west1
          zones:
            - code: us-west1-a
              cloudInfo:
                kubernetesStorageClass: yb-standard
                kubeNamespace: anabaria-devspace
            - code: us-west1-b
              cloudInfo:
                kubernetesStorageClass: yb-standard
                kubeNamespace: anabaria-devspace
            - code: us-west1-c
              cloudInfo:
                kubernetesStorageClass: yb-standard
                kubeNamespace: anabaria-devspace
    ```

### Create a universe

Use the YBUniverse CRD to create a universe using the `kubectl apply` command:

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
  # Use your YBA version
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
  kubernetesOverrides: {}
  tserverResourceSpec:
    cpu: 4
    memory: 16 
  masterResourceSpec:
    cpu: 2
    memory: 4
```

To check the status of the universe, do the following:

```sh
kubectl get ybuniverse  -n yb-operator
```

```output
NAME                     STATE   SOFTWARE VERSION
operator-universe-demo   Ready   {{< yb-version version="stable" format="build">}}
```

To modify the universe, edit the CRD and use `kubectl apply/edit` operations.

To change storage class or volume count on a running universe, use the `tserverVolume` and `masterVolume` fields (including optional `perAZ` overrides). Refer to [Full move for Kubernetes universes](../../manage-deployments/kubernetes-full-move/#operator-universes).

### Create a universe with placement information

Starting from YugabyteDB Anywhere v2025.2, you can specify `placementInfo` in the YBUniverse CRD to control regional and zonal placement of nodes. Use `defaultRegion` and `regions` with zone-level `numNodes` and optional `preferred` to define where nodes are placed. You need a Kubernetes provider (for example, one created via [YBProvider](#create-a-provider)) and set `spec.providerName` to its name.

Starting in v2026.1, `placementInfo` also supports multi-region universes that span multiple Kubernetes clusters. Configure per-zone kubeconfigs in your YBProvider via [`kubeConfigSecret`](#using-a-custom-kubeconfig). Multi-cluster deployments require proper network connectivity between clusters; see [Configure Kubernetes multi-cluster environment](../../configure-yugabyte-platform/kubernetes/#configure-kubernetes-multi-cluster-environment) and [Networking for Kubernetes](../../prepare/networking-kubernetes/).

```sh
kubectl apply universedemo-placement.yaml -n yb-platform
```

```yaml
# universedemo-placement.yaml
apiVersion: operator.yugabyte.io/v1alpha1
kind: YBUniverse
metadata:
  name: operator-universe-demo
spec:
  placementInfo:
    defaultRegion: us-west1
    regions:
    - code: us-west1
      zones:
      - code: us-west1-a
        numNodes: 2
        preferred: true
      - code: us-west1-b
        numNodes: 1
        preferred: true
  providerName: test-provider
  numNodes: 3
  replicationFactor: 3
  enableYSQL: true
  enableNodeToNodeEncrypt: true
  enableClientToNodeEncrypt: true
  ybSoftwareVersion: 2025.2.0.0-b131
  enableYSQLAuth: false
  enableYCQL: true
  enableYCQLAuth: false
  enableIPV6: false
  deviceInfo:
    numVolumes: 1
    volumeSize: 800
  gFlags:
    masterGFlags: {}
    tserverGFlags: {}
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

#### Create a universe with read replicas

{{<tags/feature/ea idea="2460">}}Starting from YugabyteDB Anywhere v2026.1, you can specify a [Read Replica](../../../architecture/key-concepts/#read-replica-cluster) cluster in the YBUniverse CR using the `readReplica` field.

```sh
kubectl apply universe-read-replica.yaml -n yb-platform
```

```yaml
# universe-read-replica.yaml
apiVersion: operator.yugabyte.io/v1alpha1
kind: YBUniverse
metadata:
  name: yugabyte-read-replica
spec:
  numNodes: 3
  replicationFactor: 3
  tserverResourceSpec:
    cpu: 3
    memory: 6
  masterResourceSpec:
    cpu: 3
  providerName: operator-provider
  readReplica:
    numNodes: 3
    replicationFactor: 3
    deviceInfo:
      numVolumes: 1
      volumeSize: 80
    tserverResourceSpec:
      cpu: 4
      memory: 6
  enableYSQL: true
  enableNodeToNodeEncrypt: false
  enableClientToNodeEncrypt: false
  ybSoftwareVersion: 2026.1.0.0-b0
  enableYSQLAuth: false
  enableYCQL: false
  enableYCQLAuth: false
  enableIPV6: false
  deviceInfo:
    numVolumes: 1
    volumeSize: 80
```

### Add a different software release of YugabyteDB

Use the Release CRD to add a different software release of YugabyteDB:

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
          x86_64: "https://software.yugabyte.com/releases/2.20.1.3/yugabyte-2.20.1.3-b3-linux-x86_64.tar.gz"
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

#### Service account for backup

You can attach a service account to database pods to be used to access storage in S3 or GCS. The service account used for the database pods should have annotations for the IAM role. The service account to be used can be applied to the database pods as a Helm override with provider- or universe- level overrides.

The operator pod (with the YugabyteDB Anywhere instance) should have the IAM role for cloud storage access attached to its service account.

**AWS**

The IAM role used should be sufficient to access storage in S3.

To enable IAM roles to access storage in S3, set the **Use S3 IAM roles attached to DB node for Backup/Restore** Universe Configuration option (config key `yb.backup.s3.use_db_nodes_iam_role_for_backup`) to true. Refer to [Manage runtime configuration settings](../../administer-yugabyte-platform/manage-runtime-config/).

The storage config CR should have IAM as the credential source.

```yaml
apiVersion: operator.yugabyte.io/v1alpha1
kind: StorageConfig
metadata:
  name: s3-config-operator
spec:
  config_type: STORAGE_S3
  data:
    BACKUP_LOCATION: s3://backups.yugabyte.com/test
    USE_IAM: true //For IAM based access on GCP/S3
```

Provide the service account in the universe overrides section. The service account should have IAM roles configured for access to cloud storage.

```yaml
apiVersion: operator.yugabyte.io/v1alpha1
kind: YBUniverse
metadata:
  name: operator-universe
spec:
  ...
  kubernetesOverrides:
    tserver:
      serviceAccount: <KSA_NAME>
```

For more information, refer to [Enable IAM roles for service accounts](https://docs.aws.amazon.com/eks/latest/userguide/enable-iam-roles-for-service-accounts.html) in the AWS documentation.

**GKE**

The IAM role used should be sufficient to access storage in GCS.

The storage config CR should have IAM as the credential source.

```yaml
apiVersion: operator.yugabyte.io/v1alpha1
kind: StorageConfig
metadata:
  name: gcs-config-operator
spec:
  config_type: STORAGE_GCS
  data:
    BACKUP_LOCATION: gs://gcp-bucket/test_backups
    USE_IAM: true //For IAM based access on GCP/S3
```

Provide the service account in the universe overrides section. The service account should have IAM roles configured for access to cloud storage.

```yaml
apiVersion: operator.yugabyte.io/v1alpha1
kind: YBUniverse
metadata:
  name: operator-universe
spec:
  ...
  kubernetesOverrides:
    tserver:
      serviceAccount: <KSA_NAME>
```

For more information, refer to [Authenticate to Google Cloud APIs from GKE workloads](https://cloud.google.com/kubernetes-engine/docs/how-to/workload-identity) in the Google Cloud documentation.

#### Scheduled backups

This feature is {{<tags/feature/ea idea="1448">}}. Backup schedules support taking full backups based on cron expressions or specified frequencies. They also allow you to configure incremental backups to run in between these full backups, providing finer-grained recovery points.

When an operator schedule triggers a backup, a corresponding CR is automatically created for that specific backup. The operator names this CR appropriately, and marks it with "ignore-reconciler-add".

Operator schedules maintain owner references to their respective YugabyteDB Anywhere universes. This ensures that when you delete a source universe, its associated schedule is also deleted.

The operator's backup schedule also supports Point-In-Time Recovery (PITR) from a backup. See [Create a scheduled backup policy with PITR](../../back-up-restore-universes/schedule-data-backups/#create-a-scheduled-backup-policy-with-pitr) for more details.

**Setup**

Set up scheduled backups as follows:

1. Apply latest CRDs with new scheduled backups CRD on the Kubernetes cluster.

    ```sh
    kubectl apply -f https://raw.github.com/yugabyte/charts/{{< yb-version version="stable" format="short">}}/crds/concatenated_crd.yaml
    ```

1. Verify scheduled backup fields in the BackupSchedule CRD specification using `kubectl explain` to understand the available configuration options.

    ```sh
    $ kubectl explain backupschedules.operator.yugabyte.io.spec
    ```

    ```output
    GROUP:operator.yugabyte.io
    KIND:BackupSchedule
    VERSION:v1alpha1
    FIELDS:
      backupType<string>-required-
        Type of backup to be taken. Allowed values are - YQL_TABLE_TYPE
        PGSQL_TABLE_TYPE

      cronExpression<string>
        Frequency of full backups in cron expression.

      enablePointInTimeRestore<boolean>
        Enable Point in time restore for backups created with the schedule

      incrementalBackupFrequency<integer>
        Frequency of incremental backups in milliseconds

      keyspace<string>-required-
        Name of keyspace to be backed up.

      schedulingFrequency<integer>
        Frequency of full backups in milliseconds.

      storageConfig<string>-required-
        Storage configuration for the backup, refers to a storageconfig CR name. Should be in the same namespace as the backupschedule.

      tableByTableBackup<boolean>
        Boolean indicating if backup is to be taken table by table.

      timeBeforeDelete<integer>
        Time before backup is deleted from storage in milliseconds.

      universe<string>-required-
        Name of the universe for which backup is to be taken, refers to a ybuniverse CR name. Should be in the same namespace as the backupschedule.
    ```

##### Example

This example describes how to create and delete scheduled backups, and assumes you have the following:

- An existing YugabyteDB Anywhere universe deployed using the YugabyteDB Kubernetes Operator.
- A configured storage location for your backups.

Use the following CRD to create a scheduled backup:

```sh
kubectl apply scheduled-backup-demo.yaml -n schedule-cr
```

```yaml
apiVersion:operator.yugabyte.io/v1alpha1
kind:BackupSchedule
metadata:
  name:operator-scheduled-backup-1
spec:
  backupType:PGSQL_TABLE_TYPE
  storageConfig:s3-config-operator
  universe:operator-universe-test-2
  timeBeforeDelete:1234567890
  keyspace:test
  schedulingFrequency:3600000
  incrementalBackupFrequency:900000
```

Backups are created from the schedules (using their auto-created CRs). You can verify them using the `kubectl get backups` as follows:

```sh
kubectl get backups -n schedule-cr
```

```output
NAME                                                          AGE
operator-scheduled-backup-1-1069296176-full--06-43-25         32m
operator-scheduled-backup-1-1069296176-incremental--06-59-26  16m
operator-scheduled-backup-1-1069296176-incremental--07-13-26  2m55s
```

Backup schedules get automatically deleted when you delete the YBA universe that owns it as per the following:

```sh
$kubectl get backups -n schedule-cr
```

```yaml
NAME                           AGE
operator-scheduled-backup-1   101m
```

```sh
$ kubectl get ybuniverse -n schedule-cr
```

```yaml
NAME                       STATE   SOFTWARE VERSION
operator-universe-test-2   Ready   2.25.2.0-b40
```

```sh
# Delete YBA universe
$ kubectl delete ybuniverse operator-universe-test-2 -n schedule-cr
```

```output
ybuniverse.operator.yugabyte.io "operator-universe-test-2" deleted
```

```sh
$ kubectl get backupschedule -n schedule-cr
```

```output
No resources found in schedule-cr namespace.
```

#### Incremental backups

{{<tags/feature/ea idea="1448">}}Use backup schedules to schedule full backups at specific intervals or using a cron expression. You can also configure incremental backups to run in between these full backups, providing finer-grained recovery points.

This functionality creates a chain of references for your backups. Each incremental backup CR references its preceding backup in the chain, whether a full or another incremental backup. This chain always leads back to the initial full backup.

When you initiate an incremental backup, it is appended to the last successful backup (either full or incremental) within that existing chain. This ensures a consistent and complete backup history.

To delete the backups, you delete the first full backup, and this action triggers a chain of deletes. You cannot delete individual incremental backups, as doing so can break the backup chain.

**Setup**

Set up incremental backups as follows:

1. Apply the latest CRD for backup:

    ```sh
    kubectl apply -f https://raw.github.com/yugabyte/charts/{{< yb-version version="stable" format="short">}}/crds/concatenated_crd.yaml
    ```

1. Verify incremental backup fields in the backup CRD specification using `kubectl explain` to understand the available configuration options.

    ```sh
    $ kubectl explain backups.operator.yugabyte.io.spec
    ```

    ```output
    GROUP: operator.yugabyte.io
    KIND: Backup
    VERSION: v1alpha1
    FIELDS:
    ...
    incrementalBackupBase <string>
      Base backup Custom Resource name. Operator will add an incremental backup to the existing chain of backups at the last.
    ```

##### Example

This example describes how to create and delete incremental backups, and assumes you have the following:

- An existing YugabyteDB Anywhere universe deployed using the YugabyteDB Kubernetes Operator.
- A configured storage location for your backups. You must have an existing base backup (either a full backup or a previous incremental backup) to create the new incremental backup.

Use the following CRD to create an incremental backup:

```sh
kubectl apply operator-backup-demo.yaml -n schedule-cr
```

```yaml
#operator-backup-demo.yaml
apiVersion: operator.yugabyte.io/v1alpha1
kind: Backup
metadata:
  name: operator-backup-1
spec:
  backupType: PGSQL_TABLE_TYPE
  storageConfig: az-config-operator-1
  universe: operator-universe-test-1
  timeBeforeDelete: 1234567890
  keyspace: test
  incrementalBackupBase: <base full backup cr name>
  ```

Deleting full backup deletes all incremental backups associated with it as follows:

```sh
# Get all backups in the 'schedule-cr' namespace.
$ kubectl get backups -n schedule-cr
```

```output
NAME                                                                     AGE
operator-scheduled-backup-1-1069296176-full-2025-02-27-06-43-25          32m
operator-scheduled-backup-1-1069296176-incremental-2025-02-27-06-59-26   16m
operator-scheduled-backup-1-1069296176-incremental-2025-02-27-07-13-26   2m55s
```

```sh
$ kubectl delete backup operator-scheduled-backup-1-1069296176-full-2025-02-27-06-43-25 -n schedule-cr
```

```sh
$ kubectl get backups -n schedule-cr
```

```output
No resources found in schedule-cr namespace.
```

### Configure PITR

Use the PitrConfig CRD to configure point-in-time recovery (PITR) for a universe. Declarative operations include creating a PITR configuration, updating the list of databases, and deleting the configuration.

Starting from YugabyteDB Anywhere v2026.1, you can also trigger a PITR restore using the [PitrRestore CR](#restore-from-pitr).

#### Create a PITR configuration

```sh
kubectl apply pitr-config.yaml -n test-pitr
```

```yaml
# pitr-config.yaml
apiVersion: operator.yugabyte.io/v1alpha1
kind: PitrConfig
metadata:
  name: pitr-config
  namespace: test-pitr
spec:
  name: pitr-config
  universe: test-universe
  database: 'yugabyte'
  tableType: 'YSQL'
```

#### Restore from PITR

{{<tags/feature/ea idea="2460">}}Starting from YugabyteDB Anywhere v2026.1, use the PitrRestore CRD to restore a universe to a state back in time when PITR is enabled for a database.

1. Create a universe:

    ```sh
    kubectl apply pitr-universe.yaml -n test-pitr
    ```

    ```yaml
    # pitr-universe.yaml
    apiVersion: operator.yugabyte.io/v1alpha1
    kind: YBUniverse
    metadata:
      name: pitr-universe
    spec:
      universeName: "pitr-universe"
      numNodes: 1
      replicationFactor: 1
      enableYSQL: true
      enableNodeToNodeEncrypt: true
      enableClientToNodeEncrypt: true
      enableLoadBalancer: false
      ybSoftwareVersion: "2026.1.0.0-b0"
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

1. Create a PITR configuration:

    ```sh
    kubectl apply pitr-config.yaml -n test-pitr
    ```

    ```yaml
    # pitr-config.yaml
    apiVersion: operator.yugabyte.io/v1alpha1
    kind: PitrConfig
    metadata:
      name: pitr-config
    spec:
      name: pitr-config
      universe: pitr-universe
      database: 'yugabyte'
      tableType: 'YSQL'
    ```

1. Trigger a PITR restore:

    ```sh
    kubectl apply pitr-restore.yaml -n test-pitr
    ```

    ```yaml
    # pitr-restore.yaml
    apiVersion: operator.yugabyte.io/v1alpha1
    kind: PitrRestore
    metadata:
      name: my-pitr-restore
    spec:
      universe: pitr-universe
      pitrConfig: pitr-config
      restoreTime: "2026-02-27T12:50:00Z"
    ```

### Configure xCluster DR

{{<tags/feature/ea idea="2460">}}Starting from YugabyteDB Anywhere v2026.1, use the DrConfig CRD to create and manage [xCluster DR](../../back-up-restore-universes/disaster-recovery/) configurations. Both declarative operations (create, update the database list, delete) and imperative operations (switchover, failover, pause/resume, restart, replace replica) are supported.

Before you create a DrConfig CR, ensure that the source and target universes and the storage configuration referenced in the CR exist. The following sections describe the DrConfig CR changes for each supported operation.

#### Create a DR configuration

```sh
kubectl apply dr-config.yaml -n yb-platform
```

```yaml
# dr-config.yaml
apiVersion: operator.yugabyte.io/v1alpha1
kind: DrConfig
metadata:
  name: prod-to-dr-config
spec:
  name: prod-to-dr-config
  sourceUniverse: dr-source
  targetUniverse: dr-target
  databases:
    - "db1"
  storageConfig: trial-backup-config
```

#### Edit the database list

Update the `databases` list in the DrConfig CR and apply the change:

```yaml
apiVersion: operator.yugabyte.io/v1alpha1
kind: DrConfig
metadata:
  name: prod-to-dr-config
spec:
  name: prod-to-dr-config
  sourceUniverse: dr-source
  targetUniverse: dr-target
  databases:
    - "db1"
    - "db2"
  storageConfig: trial-backup-config
```

#### Switchover

Swap `sourceUniverse` and `targetUniverse` in the CR to initiate a switchover:

```yaml
apiVersion: operator.yugabyte.io/v1alpha1
kind: DrConfig
metadata:
  name: prod-to-dr-config
spec:
  name: prod-to-dr-config
  sourceUniverse: dr-target
  targetUniverse: dr-source
  databases:
    - "db1"
    - "db2"
  storageConfig: trial-backup-config
```

#### Failover

Fail over to the replica:

1. Set the current target as `sourceUniverse` and use a null string (`""`) for `targetUniverse` to initiate a failover:

    ```yaml
    apiVersion: operator.yugabyte.io/v1alpha1
    kind: DrConfig
    metadata:
      name: prod-to-dr-config
    spec:
      name: prod-to-dr-config
      sourceUniverse: dr-source
      targetUniverse: ""
      databases:
        - "db1"
        - "db2"
      storageConfig: trial-backup-config
    ```

1. Restart DR after failover.

    After the universe from which failover was performed (`dr-target`) is in the Ready state, add it back as `targetUniverse`, replacing the empty string. This initiates a restart DR operation and resumes replication:

    ```yaml
    apiVersion: operator.yugabyte.io/v1alpha1
    kind: DrConfig
    metadata:
      name: prod-to-dr-config
    spec:
      name: prod-to-dr-config
      sourceUniverse: dr-source
      targetUniverse: dr-target
      databases:
        - "db1"
        - "db2"
      storageConfig: trial-backup-config
    ```

#### Replace the DR replica

To change the DR replica universe, set `targetUniverse` to the new target universe. Replication is re-established to the new target:

```yaml
apiVersion: operator.yugabyte.io/v1alpha1
kind: DrConfig
metadata:
  name: prod-to-dr-config
spec:
  name: prod-to-dr-config
  sourceUniverse: dr-source
  targetUniverse: dr-third
  databases:
    - "db1"
    - "db2"
  storageConfig: trial-backup-config
```

#### Pause and resume replication

Set `paused: true` to pause replication. When a DR config is created, `paused` defaults to `false`.

```yaml
apiVersion: operator.yugabyte.io/v1alpha1
kind: DrConfig
metadata:
  name: prod-to-dr-config
spec:
  name: prod-to-dr-config
  sourceUniverse: dr-source
  targetUniverse: dr-third
  paused: true
  databases:
    - "db1"
    - "db2"
  storageConfig: trial-backup-config
```

Set `paused: false` to resume a paused replication:

```yaml
apiVersion: operator.yugabyte.io/v1alpha1
kind: DrConfig
metadata:
  name: prod-to-dr-config
spec:
  name: prod-to-dr-config
  sourceUniverse: dr-source
  targetUniverse: dr-third
  paused: false
  databases:
    - "db1"
    - "db2"
  storageConfig: trial-backup-config
```

### Configure TLS certificates

Use the YBCertificate CRD to configure TLS certificates for encryption in transit:

```sh
kubectl apply yb-certificate.yaml -n yb-operator
```

```yaml
# yb-certificate.yaml
apiVersion: operator.yugabyte.io/v1alpha1
kind: YBCertificate
metadata:
  name: yb-certificate
spec:
  certType: SELF_SIGNED   # SELF_SIGNED | K8S_CERT_MANAGER
  certificateSecretRef:
    name: cert_secret      # Name of the secret
    namespace: yb-operator # Optional: defaults to operator namespace
```

### Support bundle

Use the SupportBundle CRD to create a [support bundle](../../troubleshoot/universe-issues/#use-support-bundles):

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

## Import universe

Available in YugabyteDB Anywhere v2025.2.2 and later.

Use the operator import universe feature to import existing YugabyteDB Anywhere Kubernetes universes that are managed via Helm charts to be managed by the Kubernetes Operator. After import, change storage class or volume count using the Operator CRD fields described in [Full move for Kubernetes universes](../../manage-deployments/kubernetes-full-move/#operator-universes).

Currently, universes with any of the following configurations are not supported for import:

- Universes configured in an [xCluster](../../../architecture/docdb-replication/async-replication/) setup.
- Universes with a [Read Replica](../../../architecture/key-concepts/#read-replica-cluster) cluster.
- Universes configured with availability zone (AZ) level overrides.

### Before you begin

- Install the operator. The operator must be enabled on your instance. See [Install Kubernetes Operator](#install-kubernetes-operator).
- Verify namespace configuration.
  - If the operator is configured to watch a single, specific namespace, the namespace provided in the import payload must match that runtime configuration (for example, `yb.kubernetes.operator.namespace`).
  - If the operator is not watching a specific namespace, the payload should be the namespace you want the resources to be created in.
- Update universe CRDs. You must update the CRDs for your Kubernetes universe to the latest for the version of YugabyteDB Anywhere you have installed. Failure to do so may result in scaling down of pod resources during future edit operations.

{{< warning title="Import cannot be reversed" >}}
After a universe and its related resources are imported to be managed by the operator most edit operations are allowed only via the operator. The API and UI block edit actions on the imported resource. This operation _cannot_ be reversed.
{{< /warning >}}

### Import

To perform an operator import, you use the YugabyteDB Anywhere API.

You need an [API token](../#authentication) to authenticate when calling the endpoints, and [account details](../#account-details).

In the following commands, replace the following values:

| <div style="width:150px">Replace</div> | With |
| :--- | :--- |
| `<platform-url>` | The URL of your YugabyteDB Anywhere instance. |
| `<customer-uuid>` | Your customer UUID. |
| `<universe-uuid>` | The UUID of the universe to import. |
| `<api-token>` | Your API token. |
| `<namespace>` | The Kubernetes namespace where the custom resources will be created.<br>This must be a namespace the operator is watching; when set, this corresponds to the runtime configuration `yb.kubernetes.operator.namespace`. |

#### Precheck

Run the precheck to ensure the universe is eligible for import. Returns HTTP 200 on success.

An example API request is as follows:

```sh
curl --request POST \
  --url https://<platform-url>/api/v2/customers/<customer-uuid>/universes/<universe-uuid>/operator-import/precheck \
  --header 'Accept: application/json' \
  --header 'Content-Type: application/json' \
  --header 'X-AUTH-YW-API-TOKEN: <api-token>' \
  -d '{"namespace": "<namespace>"}'
```

#### Import

Creates operator resources for the universe in the given namespace. Returns a task UUID and resource UUID.

An example API request is as follows:

```sh
curl --request POST \
  --url https://<platform-url>/api/v2/customers/<customer-uuid>/universes/<universe-uuid>/operator-import \
  --header 'Accept: application/json' \
  --header 'Content-Type: application/json' \
  --header 'X-AUTH-YW-API-TOKEN: <api-token>' \
  -d '{"namespace": "<namespace>"}'
```

### Resources imported

Importing a universe to the operator creates or adopts the following in the target namespace:

- Universe.
- Provider, if all universes managed by that provider are being brought under operator control.
- Backups.
- Backup schedules.
- Storage configurations related to the backups or backup schedules, including secrets to access the storage configuration.
- Release, including secrets to access the release.

## Limitations

- Currently, YugabyteDB Kubernetes Operator does not support the following features:
  - Software upgrade rollback
  - [Encryption-At-Rest](../../security/enable-encryption-at-rest/)
- Only self-signed [encryption in transit](../../security/enable-encryption-in-transit/) is supported. Editing this later is not supported.

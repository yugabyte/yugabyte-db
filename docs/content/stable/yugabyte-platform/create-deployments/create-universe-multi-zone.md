---
title: Create a universe using YugabyteDB Anywhere
headerTitle: Create universes
linkTitle: Create universes
description: Use YugabyteDB Anywhere to create YugabyteDB universes (Classic UI).
headcontent: Deploy universes on VMs in YugabyteDB Anywhere
aliases:
  - /stable/yugabyte-platform/create-deployments/create-universe-multi-region/
  - /stable/yugabyte-platform/create-deployments/create-universe-multi-zone-kubernetes/
menu:
  stable_yugabyte-platform:
    identifier: create-universes-2-legacy
    parent: create-deployments
    weight: 10
rightNav:
  hideH4: true
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../create-universes-wizard/" class="nav-link">
      New UI
    </a>
  </li>

  <li>
    <a href="../create-universe-multi-zone/" class="nav-link active">
      Classic UI
    </a>
  </li>
</ul>

{{<tags/ui/classic>}} YugabyteDB Anywhere allows you to create a universe across multiple availability zones in a single geographic region, or spanning multiple regions (for example, Oregon, South Carolina, and Tokyo), using a provider configuration. This includes universes deployed on VMs (AWS, GCP, Azure, or on-premises) and on Kubernetes.

For specific scenarios such as creating large numbers of tables, high rates of DDL change, and so on, consider creating a universe with dedicated nodes for YB-Master processes.

For planning considerations, including topology, hardware, and security, refer to [Plan your universe](../create-universes-overview/).

For information on modifying or scaling an existing universe, refer to [Modify universe](../../scale-deployments/edit-universe/).

## Prerequisites

Before you start creating a universe, ensure that you have created a provider configuration as described in [Create provider configurations](../../configure-yugabyte-platform/). For Kubernetes universes, see [Create Kubernetes provider configuration](../../configure-yugabyte-platform/kubernetes/).

## Create a universe

To create a universe:

1. Navigate to **Dashboard** or **Universes**, and click **Create Universe**.

1. Enter the universe details. Refer to [Universe settings](#universe-settings).

1. To add a read replica, click **Configure Read Replica**. Refer to [Create a read replica cluster](../read-replicas-classic/).

1. Click **Create** when you are done and wait for the configuration to complete.

![Create Universe on GCP](/images/yp/create-uni-multi-zone.png)

## Universe settings

### Cloud Configuration

Specify the provider and geolocations for the nodes (or pods, for Kubernetes) in the universe:

- Enter a name for the universe.

- Choose the [provider configuration](../../configure-yugabyte-platform/) to use to create the universe.

- Select the regions in which to deploy nodes. The available regions will depend on the provider you selected.

- Specify the master placement for the YB-Master processes. Refer to [Create a universe with dedicated nodes](../dedicated-master/) for more details. (Not applicable to Kubernetes.)

- Enter the number of nodes to deploy in the universe. When you provide the value in the **Nodes** field (or **TServer** for Kubernetes), the nodes are automatically placed across all the availability zones to guarantee the maximum availability.

- Select the [replication factor](../../../architecture/docdb-replication/replication/#replication-factor) for the universe. For help choosing RF and topology, refer to [Placement](../create-universes-overview/#placement).

- Configure the availability zones where the nodes will be deployed by clicking **Add Zone**.

- Use the **Preferred** setting to set the [preferred zone or region](../create-universes-overview/#preferred-region).

### Instance Configuration

#### VM providers

Specify the instance to use for the universe nodes:

- Choose the **CPU Architecture**, either x86 (Intel) or AArch64 (ARM).
- Choose the **Linux version** to be provisioned on the nodes of the universe.

  This option only applies if you have selected an AWS, GCP, or Azure provider configuration. The available Linux versions are specified in the provider.

  If you are performing an airgapped installation, you cannot use YBA-Managed Linux versions; you must use a custom image. Do the following before creating your universe:

  1. Create a custom Linux version (AMI) that includes all of the software pre-requisites, including [additional software for airgapped deployment](../../prepare/server-nodes-software/#additional-software-for-airgapped-deployment).
  1. Add your custom Linux version to the universe provider configuration **Linux Version Catalog**.

  Refer to [Create cloud provider configuration](../../configure-yugabyte-platform/aws/).

- Select the **Instance Type** to use for the nodes in the universe.
- Specify the number and size of the storage volumes, and the storage type.

##### Additional AWS fields

- Choose the AWS **EBS Type** between IO1, IO2, GP2, and GP3.
- Specify the **Provisioned IOPS** (IO1, IO2, and GP3 only) and **Provisioned Throughput** (GP3 only) for your disk in advance to ensure a consistent performance level.
- {{<tags/feature/ea idea="2329">}}Enable **EBS Volume Encryption** (AWS only) to create a universe with AWS EBS volume-level encryption, using a custom AWS Key Management Service (KMS) configuration.

  Select the **Key Management Service Config** you created. See [Create a KMS configuration](../../security/create-kms-config/aws-kms/#create-a-kms-configuration).

  While in Early Access, EBS Volume Encryption is not available in YugabyteDB Anywhere by default. To make it available, set the _Allow Cloud Volume Encryption_ Global Runtime Configuration option (config key `yb.universe.allow_cloud_volume_encryption`) to true. Refer to [Manage runtime configuration settings](../../../yugabyte-platform/administer-yugabyte-platform/manage-runtime-config/). You must be a Super Admin to set global runtime configuration flags.

  You can use AWS EBS volume-level encryption and YugabyteDB Anywhere envelope [Encryption at rest](../../security/enable-encryption-at-rest/) (EAR) at the same time. Configure each one with its own KMS config; you cannot use the same KMS config for both.

  Currently, you cannot use EBS volume-level encryption for multi-region universe deployments, because an instance in one region cannot access the KMS key in another region.

#### Kubernetes

Complete the **Instance Configuration** section for **TServer** and **Master** as follows:

- **Number of Cores** - specify the total number of processing cores or CPUs assigned to the TServer and Master.
- **Memory(GiB)** - specify the memory allocation of the TServer and Master.
- **Volume Info** - specify the number of volumes multiplied by size for the TServer and Master. The default is 1 x 100GB.

  After the universe is created, you can change storage class and volume count on universes running YugabyteDB v2026.1.0.0 or later. Refer to [Full move for Kubernetes universes](../../scale-deployments/kubernetes-full-move/).

YugabyteDB supports ARM instances on Kubernetes, which are specified using Helm overrides. Refer to [Kubernetes overrides](../../scale-deployments/edit-helm-overrides/#arm-vms).

### Security Configurations

#### IP Settings

To enable public access to the universe, select the **Assign Public IP** option. (AWS, GCP, or Azure only.)

#### Authentication Settings

Enable the YSQL and YCQL endpoints and database authentication.

Enter the password to use for the default database admin superuser (for YSQL the user is `yugabyte`, and for YCQL `cassandra`). Be sure to save your password; the password is not saved in YugabyteDB Anywhere. For more information, refer to [Database authorization](../../security/authorization-platform/).

By default, the API endpoints use ports 5433 (YSQL) and 9042 (YCQL). You can [customize these ports](#advanced-configuration).

#### Encryption Settings

Enable encryption in transit to encrypt universe traffic. You can enable the following:

- **Node-to-Node TLS** to encrypt traffic between universe nodes.
- **Client-to-Node TLS** to encrypt traffic between universe nodes and external clients.

    Note that if you want to enable Client-to-Node encryption, you first must enable Node-to-Node encryption.

Encryption requires a certificate. YugabyteDB Anywhere can generate a self-signed certificate automatically, or you can use your own certificate.

To use your own, you must first add it to YugabyteDB Anywhere; refer to [Add certificates](../../security/enable-encryption-in-transit/add-certificate-self/).

To have YugabyteDB Anywhere generate a certificate for the universe, use the default **Root Certificate** setting of **Create New Certificate**. To use a certificate you added or a previously generated certificate, select it from the **Root Certificate** menu.

For more information on using and managing certificates, refer to [Encryption in transit](../../security/enable-encryption-in-transit/).

To encrypt the universe data, select the **Enable encryption at rest** option and select the [KMS configuration](../../security/create-kms-config/aws-kms/) to use for encryption. For more information, refer to [Encryption at rest](../../security/enable-encryption-at-rest/).

### Advanced Configuration

DB Version
: Choose the version of YugabyteDB to install on the nodes. If the version you want is not listed, import it into YugabyteDB Anywhere. Refer to [YugabyteDB version](../create-universes-overview/#yugabytedb-version) and [Manage YugabyteDB releases](../../manage-deployments/ybdb-releases/).

Access key
: The access key is the SSH key that is created in the provider. Usually, each provider has its own access key, but if you are reusing keys across providers, they are listed here. (Not applicable to Kubernetes.)

Instance Profile ARN
: For AWS providers, you can assign an ARN to the nodes in the universe; this allows them to be seamlessly backed up without explicit credentials.

Enable IPV6
: For Kubernetes universes, specify whether to use IPv6 networking for connections between database servers. This setting is disabled by default.

Enable Public Network Access
: For Kubernetes universes, specify whether to assign a load balancer or nodeport for connecting to the database endpoints over the Internet. This setting is disabled by default.

Enhanced Postgres Compatibility
: If database version is v2024.2 or later, you can enable early access features for PostgreSQL compatibility. For more information, refer to [Enhanced PostgreSQL Compatibility Mode](../../../reference/configuration/postgresql-compatibility/).
: For new universes running v2025.2 or later, note that the following features are _enabled by default_ when you deploy using YugabyteDB Anywhere:

- [Read committed](../../../architecture/transactions/read-committed/)
- [Cost-based optimizer](../../../best-practices-operations/ysql-yb-enable-cbo/)
- [Auto Analyze](../../../additional-features/auto-analyze/)
- [YugabyteDB bitmap scan](../../../reference/configuration/postgresql-compatibility/#yugabytedb-bitmap-scan)
- [Parallel append](../../../additional-features/parallel-query/)

Enable Connection Pooling
: If database version is v2024.2 or later, you can enable [Built-in connection pooling](../../../additional-features/connection-manager-ysql/).
: After the universe is created, you can customize additional connection pooling YB-TServer flags using [Edit configuration flags](../../scale-deployments/edit-config-flags/). For flag names and defaults, refer to [YSQL Connection Manager configuration](../../../additional-features/connection-manager-ysql/ycm-setup/#configure).

Override Deployment Ports
: To customize the [ports used for the universe](../../prepare/networking/), select the **Override Deployment Ports** option and enter the custom port numbers for the services you want to change. Any value from `1024` to `65535` is valid, as long as it doesn't conflict with anything else running on nodes to be provisioned.

### G-Flags

Optionally, add configuration flags for your YB-Master and YB-TServer nodes. You can also set flags after universe creation. Refer to [Edit configuration flags](../../scale-deployments/edit-config-flags/).

{{< tip title="Multi-region deployments" >}}

Because data is globally replicated, RPC latencies are higher. For multi-region universes, consider increasing the failure detection interval by setting the following flags for both Master and TServer:

```properties
leader_failure_max_missed_heartbeat_periods=5
raft_heartbeat_interval_ms=1500
leader_lease_duration_ms=6000
```

{{< /tip >}}

### Helm Overrides

For Kubernetes universes, you can optionally set Helm chart overrides when creating the universe. Refer to [Configure Kubernetes overrides](../../scale-deployments/edit-helm-overrides/).

### User Tags

The instances created on a cloud provider can be assigned special metadata to help manage, bill, or audit the resources. You can define these tags when you create a new universe, as well as modify or delete tags of an existing universe. Refer to [Create and edit instance tags](../../scale-deployments/instance-tags/). (Not applicable to Kubernetes.)

## Deploy immutable YB Controller on Kubernetes

By default, YugabyteDB Anywhere deploys YB Controller (YBC) on Kubernetes universes by copying the YBC package from YugabyteDB Anywhere to the database pods and extracting it. While this approach ensures a stable YBC version, it has some limitations:

- Does not follow Kubernetes standards for container processes.
- Performs package copy operations on running containers.
- If a Persistent Volume Claim (PVC) gets deleted or replaced, YBC may not be available until YugabyteDB Anywhere detects the issue and re-uploads YBC (for example, before a backup operation if YBC ping failures are detected).

For deployments following strict Kubernetes practices, or when you want YBC to be automatically available even after PVC replacement, you can enable **Immutable YBC**. With this feature, YBC is baked into the YugabyteDB image and runs as a native process alongside `yb-master` and `yb-tserver`, similar to other database processes.

{{< note title="Important" >}}

When immutable YBC is enabled, the YBC version is tied to the YugabyteDB version used by the universe, and is upgraded only when you [upgrade the universe](../../manage-deployments/upgrade-software/). YBC will not automatically update when upgrading YugabyteDB Anywhere.

{{< /note >}}

### Enable YBC immutability

**For new universes:**

Set the `useYbdbInbuiltYbc` field in the `userIntent` object of the primary cluster when sending the Create Universe API request. An example API request is as follows:

```sh
curl --request POST \
  --url https://<yugabyte-platform-url>/api/v1/customers/<customer-uuid>/universes \
  --header 'Accept: application/json' \
  --header 'Content-Type: application/json' \
  --header 'X-AUTH-YW-API-TOKEN: <api-token>' \
  -d '{
    "clusters": [{
      "userIntent": {
        "universeName": "my-k8s-universe",
        "provider": "<provider-uuid>",
        "providerType": "kubernetes",
        "useYbdbInbuiltYbc": true,
        // ... other required fields
      }
    }]
  }'
```

For more information, refer to the [Create Universe API documentation](https://api-docs.yugabyte.com/docs/yugabyte-platform/4548b5e5061a8-create-universe-clusters).

**For existing universes:**

Use the Kubernetes Toggle Immutability API to switch Immutable YBC on or off. To enable the feature, an example API request is as follows:

```sh
curl --request POST \
  --url https://<yugabyte-platform-url>/api/v1/customers/<customer-uuid>/universes/<universe-uuid>/upgrade/k8s_immutable_ybc \
  --header 'Accept: application/json' \
  --header 'Content-Type: application/json' \
  --header 'X-AUTH-YW-API-TOKEN: <api-token>' \
  -d '{"useYbdbInbuiltYbc": true}'  # Set to false to disable Immutable YBC
```

Replace:

- `<yugabyte-platform-url>` with your YugabyteDB Anywhere URL
- `<customer-uuid>` with your customer UUID
- `<universe-uuid>` with your universe UUID
- `<api-token>` with your API token

Set `useYbdbInbuiltYbc` to `true` to enable Immutable YBC, or `false` to disable it and revert to the package copy approach.

## Examine the universe

After the universe is ready, its **Overview** tab should appear similar to the following illustration:

![Multi-zone universe ready](/images/yp/multi-zone-universe-ready-1-220.png)

The universe page includes the following tabs:

- **Overview** — Cost, database version, primary cluster details (nodes, instance type, Linux version, and replication factor), CPU and disk usage, health check status, node geography and data placement, operations per second, average latency, and table counts.
- **Tables** — Details about YSQL and YCQL tables in the universe. Table sizes are calculated across all nodes in the cluster.
- **Nodes** (or **Pods** for Kubernetes) — Details on nodes or pods in the universe, and actions on a specific node (connect, stop, remove, display live and slow queries, download logs). You can also use **Nodes** to open the cloud provider's instances page. For example, in case of GCP, if you navigate to **Compute Engine > VM Instances** and search for instances that contain the name of your universe, you should see a list of instances.
- **Metrics** — Graphs representing operations, latency, and other parameters for each type of node and server.
- **Queries** — Live and slow queries that you can filter by column and text.
- **xCluster Disaster Recovery** — Information about any [disaster recovery](../../back-up-restore-universes/disaster-recovery/) configured for the universe.
- **xCluster Replication** — Information about any [asynchronous replication](../xcluster-replication/) in the universe.
- **Tasks** — Details about the state of tasks running on the universe, as well as tasks that have run in the past against this universe.
- **Backups** — Scheduled backups, if any, and options to create, restore, and delete backups.
- **Health** — Detailed health check status of the nodes and components involved in their operation. **Health** also allows you to pause health check alerts.

For information on connecting to nodes and database endpoints, refer to [Connect to a universe](../connect-to-universe/).

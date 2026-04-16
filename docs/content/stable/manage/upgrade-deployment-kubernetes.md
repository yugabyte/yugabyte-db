---
title: Upgrade YugabyteDB on Kubernetes
headerTitle: Upgrade YugabyteDB on Kubernetes
linkTitle: Kubernetes
description: Upgrade YugabyteDB running on Kubernetes
headcontent: Upgrade Kubernetes deployments of YugabyteDB
menu:
  stable:
    identifier: upgrade-deployment-kubernetes
    parent: manage-upgrade-deployment
    weight: 800
type: docs
---

The following steps assume a multi-zone Kubernetes cluster, deployed as described in [Deploy on Google Kubernetes Engine](../../deploy/kubernetes/multi-zone/gke/helm-chart/), but you can adapt these steps to upgrade other types of deployments.

## Example deployment

The universe is deployed across three distinct zones in us-west1: us-west1-a, us-west1-b, and us-west1-c. One Master and one TServer was provisioned in each zone using the following Helm commands.

```sh
helm install yb-demo-us-west1-a yugabytedb/yugabyte \
 --version 2024.2.2 \
 --namespace yb-demo-us-west1-a \
 -f overrides-us-west1-a.yaml --wait

helm install yb-demo-us-west1-b yugabytedb/yugabyte \
 --version 2024.2.2 \
 --namespace yb-demo-us-west1-b \
 -f overrides-us-west1-b.yaml --wait

helm install yb-demo-us-west1-c yugabytedb/yugabyte \
 --version 2024.2.2 \
 --namespace yb-demo-us-west1-c \
 -f overrides-us-west1-c.yaml --wait
```

The following is an example override file:

```yaml
isMultiAz: True

AZ: us-west1-a

masterAddresses: "yb-master-0.yb-masters.yb-demo-us-west1-a.svc.cluster.local:7100,yb-master-0.yb-masters.yb-demo-us-west1-b.svc.cluster.local:7100,yb-master-0.yb-masters.yb-demo-us-west1-c.svc.cluster.local:7100"

storage:
  master:
    storageClass: "yb-storage"
  tserver:
    storageClass: "yb-storage"

replicas:
  master: 1
  tserver: 1
  totalMasters: 3

gflags:
  master:
    placement_cloud: "gke"
    placement_region: "us-west1"
    placement_zone: "us-west1-a"
    ysql_enable_auth: true
    ysql_hba_conf_csv: "local all yugabyte trust"
  tserver:
    placement_cloud: "gke"
    placement_region: "us-west1"
    placement_zone: "us-west1-a"
    ysql_enable_auth: true
    ysql_hba_conf_csv: "local all yugabyte trust"
```

## Standard upgrade

Perform a rolling upgrade of the Master across each zone.

```sh
helm upgrade yb-demo-us-west1-a yugabytedb/yugabyte \
  --version 2024.2.3 \
  --namespace yb-demo-us-west1-a \
  -f overrides-us-west1-a.yaml \
  --set image.tag=2024.2.3.3-b4 \
  --set partition.tserver=1 \
  --wait
  
helm upgrade yb-demo-us-west1-b yugabytedb/yugabyte \
  --version 2024.2.3 \
  --namespace yb-demo-us-west1-b \
  -f overrides-us-west1-b.yaml \
  --set image.tag=2024.2.3.3-b4 \
  --set partition.tserver=1 \
  --wait
  
helm upgrade yb-demo-us-west1-c yugabytedb/yugabyte \
  --version 2024.2.3 \
  --namespace yb-demo-us-west1-c \
  -f overrides-us-west1-c.yaml \
  --set image.tag=2024.2.3.3-b4 \
  --set partition.tserver=1 \
  --wait
```

To prevent software upgrades on a TServer during a Master upgrade, configure the partition.tserver setting to equal the number of TServers in that specific zone. For more information, see [Partitioned rolling updates](https://kubernetes.io/docs/concepts/workloads/controllers/statefulset/#partitions) in the Kubernetes documentation.

Perform a rolling upgrade of the TServer across each zone.

```sh
helm upgrade yb-demo-us-west1-a yugabytedb/yugabyte \
  --version 2024.2.3 \
  --namespace yb-demo-us-west1-a \
  -f overrides-us-west1-a.yaml \
  --set image.tag=2024.2.3.3-b4 \
  --wait

helm upgrade yb-demo-us-west1-b yugabytedb/yugabyte \
  --version 2024.2.3 \
  --namespace yb-demo-us-west1-b \
  -f overrides-us-west1-b.yaml \
  --set image.tag=2024.2.3.3-b4 \
  --wait

helm upgrade yb-demo-us-west1-c yugabytedb/yugabyte \
  --version 2024.2.3 \
  --namespace yb-demo-us-west1-c \
  -f overrides-us-west1-c.yaml \
  --set image.tag=2024.2.3.3-b4 \
  --wait
```

After upgrading all the YB-Master and YB-TServer processes, monitor the cluster to ensure it is healthy. Make sure workloads are running as expected and there are no errors in the logs.

### Finalize

To complete and finalise the software upgrade, execute the following yb-admin commands on the Master leader node:

```sh
/home/yugabyte/master/bin/yb-admin --master_addresses \
  yb-master-0.yb-masters.yb-demo-us-west1-a.svc.cluster.local:7100, \
  yb-master-0.yb-masters.yb-demo-us-west1-b.svc.cluster.local:7100, \
  yb-master-0.yb-masters.yb-demo-us-west1-c.svc.cluster.local:7100  \
  promote_auto_flags

/home/yugabyte/master/bin/yb-admin --master_addresses \
  yb-master-0.yb-masters.yb-demo-us-west1-a.svc.cluster.local:7100, \
  yb-master-0.yb-masters.yb-demo-us-west1-b.svc.cluster.local:7100, \
  yb-master-0.yb-masters.yb-demo-us-west1-c.svc.cluster.local:7100 \
  upgrade_ysql
```

## YSQL major version upgrade

Upgrades that include a major change in the PostgreSQL version, such as upgrading from {{<release "2024.2">}} (based on PostgreSQL 11) to {{<release "2025.1">}} or later (based on PostgreSQL 15), require additional steps.

The following steps assume YugabyteDB v2024.2.3.3-b4 is deployed, and you are upgrading to v2025.1.1.2-b3.

### Precheck

New PostgreSQL major versions add many new features and performance improvements, but also remove some older unsupported features and data types. You can only upgrade after you remove all deprecated features and data types from your databases.

To do this:

1. Copy the new software tar package to a TServer pod temporarily. This allows the pg_upgrade binary in the new software to access the data directory.

1. Unpack the new software package.

1. Locate the pg_upgrade binary in the `postgres/bin` directory.

1. Run the compatibility check using a command similar to the following:

    ```sh
    ./pg_upgrade -d /mnt/disk0/pg_data --old-host /tmp/.yb.0.0.0.0:5433 --old-port 5433 --username yugabyte --check
    ```

    The check verifies elements such as the data directory structure, locale settings, and necessary extensions, which help ensure a smooth upgrade.

### Upgrade

After successfully completing the prechecks, remove the new software from the TServer pod and then proceed with the actual software upgrade.

1. Modify the override file in _each AZ_ as follows:

    - Set the `ysql_yb_major_version_upgrade_compatibility` flag to a value of 11 on both the Master and TServer.

    - If authentication is enabled on the cluster, update the environment variable for the pgpassfile on the master pod.

    The following shows a sample override file:

    ```yaml
    isMultiAz: True

    AZ: us-west1-a

    masterAddresses: "yb-master-0.yb-masters.yb-demo-us-west1-a.svc.cluster.local:7100,yb-master-0.yb-masters.yb-demo-us-west1-b.svc.cluster.local:7100,yb-master-0.yb-masters.yb-demo-us-west1-c.svc.cluster.local:7100"

    storage:
      master:
        storageClass: "yb-storage"
      tserver:
        storageClass: "yb-storage"

    replicas:
      master: 1
      tserver: 1
      totalMasters: 3

    master:
    extraEnv:
    - name: PGPASSFILE
      value: /mnt/disk0/.pgpass

    gflags:
      master:
        placement_cloud: "gke"
        placement_region: "us-west1"
        placement_zone: "us-west1-a"
        ysql_enable_auth: true
        ysql_hba_conf_csv: "local all yugabyte trust"
        ysql_yb_major_version_upgrade_compatibility: 11
      tserver:
        placement_cloud: "gke"
        placement_region: "us-west1"
        placement_zone: "us-west1-a"
        ysql_enable_auth: true
        ysql_hba_conf_csv: "local all yugabyte trust"
        ysql_yb_major_version_upgrade_compatibility: 11
    ```

1. Execute `helm upgrade` in each AZ:

    ```sh
    helm upgrade yb-demo-us-west1-a yugabytedb/yugabyte \
      --version 2024.2.3 \
      --namespace yb-demo-us-west1-a \
      -f overrides-us-west1-a.yaml --wait
    
    helm upgrade yb-demo-us-west1-b yugabytedb/yugabyte \
      --version 2024.2.3 \
      --namespace yb-demo-us-west1-b \
      -f overrides-us-west1-b.yaml --wait
    
    helm upgrade yb-demo-us-west1-c yugabytedb/yugabyte \
      --version 2024.2.3 \
      --namespace yb-demo-us-west1-c \
      -f overrides-us-west1-c.yaml --wait
    ```

1. To initiate the YSQL catalogue upgrade after applying the new override YAML file in each AZ, you need to set up a superuser account. Run the following YSQL command:

    ```sql
    CREATE ROLE yugabyte_upgrade WITH SUPERUSER LOGIN PASSWORD 'strongPassword#123';
    ```

1. Execute a rolling upgrade of the Master server in each zone. Ensure that the TServers are not upgraded while the Master is being upgraded.

    ```sh
    helm upgrade yb-demo-us-west1-a yugabytedb/yugabyte \
      --version 2025.1.1 \
      --namespace yb-demo-us-west1-a \
      -f overrides-us-west1-a.yaml \
      --set image.tag=2025.1.1.2-b3 \
      --set partition.tserver=1 \
      --wait

    helm upgrade yb-demo-us-west1-b yugabytedb/yugabyte \
      --version 2025.1.1 \
      --namespace yb-demo-us-west1-b \
      -f overrides-us-west1-b.yaml \
      --set image.tag=2025.1.1.2-b3 \
      --set partition.tserver=1 \
      --wait
      
    helm upgrade yb-demo-us-west1-c yugabytedb/yugabyte \
      --version 2025.1.1 \
      --namespace yb-demo-us-west1-c \
      -f overrides-us-west1-c.yaml \
      --set image.tag=2025.1.1.2-b3 \
      --set partition.tserver=1 \
      --wait
    ```

1. If authentication is enabled on the cluster, do the following:

    - Identify the master leader pod.
    - exec into the master leader pod.
    - Create the .pgpass file at the path specified by the environment variable (typically `/mnt/disk0/.pgpass`). Use the appropriate command to do this. For example:

    ```sh
    echo "*:*:*:yugabyte_upgrade:strongPassword#123" >> /mnt/disk0/.pgpass
    chmod 600 /mnt/disk0/.pgpass
    ```

1. Perform YSQL major catalog upgrade using the following command on the Master leader node:

    ```sh
    /home/yugabyte/master/bin/yb-admin --master_addresses \
      yb-master-0.yb-masters.yb-demo-us-west1-a.svc.cluster.local:7100, \
      yb-master-0.yb-masters.yb-demo-us-west1-b.svc.cluster.local:7100, \
      yb-master-0.yb-masters.yb-demo-us-west1-c.svc.cluster.local:7100 \
      upgrade_ysql_major_version_catalog
    ```

    If the catalog upgrade fails, you can roll back the catalog upgrade using the following command (and then re-trigger the upgrade process):

    ```sh
    /home/yugabyte/master/bin/yb-admin --master_addresses \
      yb-master-0.yb-masters.yb-demo-us-west1-a.svc.cluster.local:7100, \
      yb-master-0.yb-masters.yb-demo-us-west1-b.svc.cluster.local:7100, \
      yb-master-0.yb-masters.yb-demo-us-west1-c.svc.cluster.local:7100 \
      rollback_ysql_major_version_catalog
    ```

1. After a successful catalog upgrade, delete the .pgpass file from the node.

1. Execute a rolling upgrade of the TServer in each zone.

    ```sh
    helm upgrade yb-demo-us-west1-a yugabytedb/yugabyte \
      --version 2025.1.1 \
      --namespace yb-demo-us-west1-a \
      -f overrides-us-west1-a.yaml \
      --set image.tag=2025.1.1.2-b3 \
      --wait
      
    helm upgrade yb-demo-us-west1-b yugabytedb/yugabyte \
      --version 2025.1.1 \
      --namespace yb-demo-us-west1-b \
      -f overrides-us-west1-b.yaml \
      --set image.tag=2025.1.1.2-b3 \
      --wait
      
    helm upgrade yb-demo-us-west1-c yugabytedb/yugabyte \
      --version 2025.1.1 \
      --namespace yb-demo-us-west1-c \
      -f overrides-us-west1-c.yaml \
      --set image.tag=2025.1.1.2-b3 \
      --wait
    ```

1. Remove the `ysql_yb_major_version_upgrade_compatibility` flag and environment variable from the override file. Then, perform a Helm upgrade to apply these changes and unset the previously set flag and variable.

    ```yaml
    isMultiAz: True

    AZ: us-west1-a

    masterAddresses: "yb-master-0.yb-masters.yb-demo-us-west1-a.svc.cluster.local:7100,yb-master-0.yb-masters.yb-demo-us-west1-b.svc.cluster.local:7100,yb-master-0.yb-masters.yb-demo-us-west1-c.svc.cluster.local:7100"

    storage:
      master:
        storageClass: "yb-storage"
      tserver:
        storageClass: "yb-storage"

    replicas:
      master: 1
      tserver: 1
      totalMasters: 3

    gflags:
      master:
        placement_cloud: "gke"
        placement_region: "us-west1"
        placement_zone: "us-west1-a"
        ysql_enable_auth: true
        ysql_hba_conf_csv: "local all yugabyte trust"
      tserver:
        placement_cloud: "gke"
        placement_region: "us-west1"
        placement_zone: "us-west1-a"
        ysql_enable_auth: true
        ysql_hba_conf_csv: "local all yugabyte trust"
    ```

1. Run the following YSQL command to delete the superuser you created for the catalog upgrade:

    ```sql
    DROP USER IF EXISTS yugabyte_upgrade;
    ```

After upgrading all the YB-Master and YB-TServer processes, monitor the cluster to ensure it is healthy. Make sure workloads are running as expected and there are no errors in the logs.

### Finalize

To complete and finalise the software upgrade, execute the following yb-admin commands on the Master leader node:

```sh
/home/yugabyte/master/bin/yb-admin --master_addresses \
  yb-master-0.yb-masters.yb-demo-us-west1-a.svc.cluster.local:7100, \
  yb-master-0.yb-masters.yb-demo-us-west1-b.svc.cluster.local:7100, \
  yb-master-0.yb-masters.yb-demo-us-west1-c.svc.cluster.local:7100 \
  finalize_ysql_major_version_catalog

/home/yugabyte/master/bin/yb-admin --master_addresses \
  yb-master-0.yb-masters.yb-demo-us-west1-a.svc.cluster.local:7100, \
  yb-master-0.yb-masters.yb-demo-us-west1-b.svc.cluster.local:7100, \
  yb-master-0.yb-masters.yb-demo-us-west1-c.svc.cluster.local:7100 \
  promote_auto_flags

/home/yugabyte/master/bin/yb-admin --master_addresses \
  yb-master-0.yb-masters.yb-demo-us-west1-a.svc.cluster.local:7100, \
  yb-master-0.yb-masters.yb-demo-us-west1-b.svc.cluster.local:7100, \
  yb-master-0.yb-masters.yb-demo-us-west1-c.svc.cluster.local:7100 \
  upgrade_ysql
```

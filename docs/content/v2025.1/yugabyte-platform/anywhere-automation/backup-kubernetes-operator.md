---
title: Incremental backups and backup schedules
headerTitle: Incremental backups and backup schedules
linkTitle: Incremental backups
description: Incremental backups and backup schedules in YugabyteDB Kubernetes Operator.
headcontent: Create incremental backups and backup schedules in YugabyteDB Kubernetes Operator.
tags:
  feature: early-access
menu:
  v2025.1_yugabyte-platform:
    parent: yb-kubernetes-operator
    identifier: backup-kubernetes-operator
    weight: 100
type: docs
---


## Incremental backups

You can add incremental backups to your [existing full or incremental backups](../../back-up-restore-universes/back-up-universe-data/) in YugabyteDB Kubernetes Operator for your YugabyteDB Anywhere universes.

This new functionality creates a chain of references for your backups. Each incremental backup Custom Resource (CR) references its preceding backup in the chain, whether a full or another incremental backup. This chain always leads back to the initial full backup.

When you initiate an incremental backup, it is appended to the last successful backup (either full or incremental) within that existing chain. This ensures a consistent and complete backup history.

### Delete backups

To simplify backup management, you delete the first full backup, and this action triggers a chain of deletes. Note that individual incremental backups cannot be deleted on their own, as doing so can break the backup chain.

### Setup

To set up incremental backups, do the following:

1. Apply the latest YugabyteDB backup Custom Resource Definition (CRD) to your Kubernetes cluster:

    ```sh
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
      Base backup Custom Resource name. Operator will add an incremental   backup to the existing chain of backups at the last.
    ```

### Example

This section decribes an example to create and delete incremental backups.

#### Prerequisites

Before you create an incremental backup, ensure you have the following:

1. An existing YugabyteDB Anywhwere universe deployed using the YugabyteDB Kubernetes Operator.

1. A configured storage location for your backups. You must have an existing base backup (either a full backup or a previous incremental backup) to create the new incremental backup.

#### Create an incremental backup

1. Create an incremental backup CR as follows:

    ```conf
    # incremental-backup.yaml
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

1. Create a universe using the custom resource, `incremental-backup.yaml` as follows:

    ```conf
    kubectl apply -f incremental-backup.yaml
    ```

#### Delete an incremental backup

Deleting full backup deletes all incremental backups associated with it as per the following:

```
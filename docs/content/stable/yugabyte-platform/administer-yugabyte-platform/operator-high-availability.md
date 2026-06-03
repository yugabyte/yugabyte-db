---
title: YugabyteDB Anywhere Operator high availability
headerTitle: Operator high availability
description: Extend YBA high availability to synchronize Kubernetes Operator custom resources across clusters
headcontent: Synchronize operator-managed resources during YBA failover and failback
linkTitle: Operator high availability
menu:
  stable_yugabyte-platform:
    identifier: platform-operator-high-availability
    parent: administer-yugabyte-platform
    weight: 45
type: docs
---

{{<tags/feature/ea idea="2460">}}YugabyteDB Anywhere (YBA) Operator high availability extends the [YBA high availability (HA)](../high-availability/) framework to synchronize Kubernetes Operator custom resources (CRs) and their associated secrets between active and standby YBA instances. This ensures that a standby YBA instance can resume management of operator-controlled universes after a failover, without requiring you to manually recreate CRs or secrets.

## Integration with YBA HA

Operator HA is a direct extension of the existing YBA HA framework. It uses the same asynchronous backup and restore mechanisms that replicate YBA management data between active and standby instances. Operator resources are included in those backups and restored automatically when a standby instance is promoted.

Because Operator HA builds on the standard YBA HA path, improvements to YBA HA are automatically inherited by the operator, providing a unified experience for both platform state and operator-managed Kubernetes resources.

## Prerequisites

Before you can use Operator HA, ensure the following:

- [YBA HA is configured](../high-availability/) between your active and standby instances.
- The [YugabyteDB Kubernetes Operator](../../anywhere-automation/yb-kubernetes-operator/) is enabled on each YBA instance in the HA cluster.
- Each YBA instance in the HA cluster can reach the Kubernetes API server for its local cluster.

## Multi-cluster environments

Operator HA is designed for deployments where YBA instances run on _separate Kubernetes clusters_. This is common in [Multi-Cluster Services (MCS)](../../configure-yugabyte-platform/kubernetes/#configure-kubernetes-multi-cluster-environment) environments.

In a single-cluster deployment, both the active and standby YBA instances typically share access to the same Kubernetes control plane and the same CRs. Operator HA is not required in that scenario as standard [YBA HA](../high-availability/) covers it.

In a multi-cluster deployment, a failover creates a management blackout: the standby YBA instance receives YBA platform state through HA backups, but the operator CRs and secrets that define and manage universes exist only on the primary cluster's Kubernetes API. Without Operator HA, the standby instance cannot manage those universes after promotion.

Operator HA addresses this when:

- YBA instances are deployed on entirely separate Kubernetes clusters.
- The primary cluster goes offline and the standby YBA on a remote cluster must take over management of existing universes.
- The standby instance needs immediate access to the CRs and secrets (such as kubeconfigs and certificates) used to create and maintain those universes.

## What to expect on failover and failback

You can expect a streamlined transition of management capabilities when a failover or failback is triggered.

### During failover

- Resource synchronization: The standby node automatically imports and applies all necessary YAML definitions for Operator CRs and their associated secrets (such as kubeconfigs and certificates) to its local Kubernetes API.

- State alignment: The system applies "force/replace" logic to ensure the new active instance's state matches the latest source of truth from the backup, avoiding inconsistencies.

- Operator activation: After operator resources are successfully applied, the standby YBA service activates its operator thread, and resumes management of the infrastructure. You do not need to manually recreate CRs or re-import universes.

For general failover steps, see [Promote a standby instance to active](../high-availability/#promote-a-standby-instance-to-active).

### During failback

When you fail back to the original primary, Operator HA keeps operator resource state consistent across both clusters.

- Spec consistency: When you fail back to the original primary, Operator HA ensures that any edits made while the standby was active are synchronized back to the original primary. This prevents specifications from being rolled back to an outdated state.

- Lifecycle management: If a CR was deleted during the failover period, the system recognizes this state and ensures the resource is not incorrectly recreated upon failback.

## Supported resources

The HA mechanism tracks and transfers all critical YBA Operator resources, including the following:

- Universes and providers.
- Backup, scheduled backup, and PITR configurations.
- Storage configurations and YugabyteDB certificates.
- Referenced Kubernetes secrets containing credentials and tokens.

For details on each CR type, see [YugabyteDB Kubernetes Operator CRDs](../../anywhere-automation/yb-kubernetes-operator/#yugabytedb-kubernetes-operator-crds).

## Learn more

- [Enable high availability](../high-availability/)
- [YugabyteDB Kubernetes Operator](../../anywhere-automation/yb-kubernetes-operator/)

---
title: Use YugabyteDB Anywhere to retry failed tasks
headerTitle: Retry a universe task
linkTitle: Retry a universe task
description: Use YugabyteDB Anywhere to retry failed tasks.
menu:
  v2024.2_yugabyte-platform:
    identifier: retry-task
    parent: manage-deployments
    weight: 75
type: docs
---

In case of most task failures in YugabyteDB Anywhere (YBA) universes (VMs or Kubernetes), you can retry the tasks from the YBA UI or via an [equivalent API](https://api-docs.yugabyte.com/docs/yugabyte-platform/68aaf7829e04f-retry-a-universe-task).

To retry a failed task, do one of the following:

- Navigate to **Universes**, select your universe, and click **Retry task** as per the following illustration:

    ![Retry task](/images/yp/retry-task1.png)

- Navigate to **Tasks**, click **See Details** corresponding to the failed universe task, and click **Retry Task**.

    ![Retry task](/images/yp/retry-task2.png)

A retry of the original task reruns the individual steps of the task in an idempotent fashion. This should resolve a failure if it was caused by a temporary issue.

For example, a quota limit may prevent a universe scale up task, causing it to fail. When the quota limit is adjusted to the right value and the task is retried, it should succeed. If the underlying issue is more permanent, contact {{% support-platform %}}.

Critical task failures, such as scaling a universe, placement changes, software upgrades, and flag changes block other critical tasks from being run on the universe. It is recommended to retry the failed task to completion before attempting other operations on the universe, or contact {{% support-platform %}} if the task continues to fail.

Note that some operations, such as taking [universe backups](../../back-up-restore-universes/) or creating [support bundles](../../troubleshoot/universe-issues/#use-support-bundles), can always be attempted even after such failures.

For some failures, a retry of the task might continue to fail indefinitely. For example, an incorrect flag value might have been specified during a flag update operation. In this case, you would run a new flag update operation with the corrected flag value instead of retrying the failed operation. You can fix the operation parameters instead of retrying for the following operations:

- [Flag updates](../edit-config-flags/)
- [Upgrade Linux Version](../upgrade-nodes/)
- [Update Kubernetes overrides](../edit-helm-overrides/)
- [Changing the instance configuration](../edit-universe/) of a Kubernetes universe

---
title: Use YugabyteDB Anywhere to monitor tasks
headerTitle: Monitor and manage universe tasks
linkTitle: Monitor universe tasks
description: Use YugabyteDB Anywhere to monitor universe tasks
headcontent: Monitor task details and manage tasks for YugabyteDB Anywhere universes
menu:
  v2025.1_yugabyte-platform:
    identifier: retry-task
    parent: manage-deployments
    weight: 75
type: docs
---

You can view the status and details of tasks performed on your YugabyteDB Anywhere universes, including create universe, edit universe, software upgrades, flag changes, and more using the universe **Tasks** page. You can also manage these tasks by retrying failed ones or aborting those still in progress.

To view the task history for all your universes, navigate to **Tasks**; to view the tasks for a specific universe, navigate to **Universes > Universe-Name > Tasks**, as shown in the following illustration:

![Task history](/images/yp/task-history.png)

For a detailed task view, navigate to the **Tasks** page, and click the task you want to inspect. The detailed task view provides a granular breakdown of how the task executed. From this view, you can:

- Monitor subtask progress: Check the status and progress of the individual subtasks.
- Access Yugaware Logs: Click **Yugaware Log**. This takes you directly to the relevant logs page for in-depth troubleshooting.
- Retry failed tasks: Click **Retry**.
- Abort in progress tasks: Click **Abort**.

## View what was changed

For certain tasks that modify your universe, you'll find dedicated buttons in the detailed view that show you the exact changes:

- Software upgrade: Click **View Software Upgrade Changes** to see a breakdown of the modifications introduced by the upgrade, including both original and new values.

- Edit universe: Click **View Universe Changes** to display a granular breakdown of the modifications made during the universe edit.

- GFlags upgrade: Click **View GFlags Changes** to review the specific GFlags that were altered, showing both their previous and updated values.

## Manage tasks

From the detailed task view, you can also manage tasks that are running, including aborting an in-progress operation or retrying a failed task.

### Abort a running task

If a task is currently running and supports interruption, you can abort it:

1. From the **Tasks** page, click the task to display its details, or click **View Details** in the progress bar for the running task you want to abort.

1. From the detailed task view, click **Abort**.

### Retry a failed task

In case of most task failures in YugabyteDB Anywhere universes (VMs or Kubernetes), you can retry the tasks from YugabyteDB Anywhere or via an [equivalent API](https://api-docs.yugabyte.com/docs/yugabyte-platform/68aaf7829e04f-retry-a-universe-task).

To retry a failed task, do one of the following:

- Navigate to your universe, and click **Retry task** as per the following illustration:

    ![Retry task](/images/yp/retry-task1.png)

- From the **Tasks** page, click the task to display its details, and click **Retry**.

A retry of the original task reruns the individual steps of the task in an idempotent fashion. This should resolve a failure if it was caused by a temporary issue.

For example, a quota limit may prevent a universe scale up task, causing it to fail. When the quota limit is adjusted to the right value and the task is retried, it should succeed. If the underlying issue is more permanent, contact {{% support-platform %}}.

Critical task failures, such as scaling a universe, placement changes, software upgrades, and flag changes block other critical tasks from being run on the universe. It is recommended to retry the failed task to completion before attempting other operations on the universe, or contact {{% support-platform %}} if the task continues to fail.

Note that some operations, such as taking [universe backups](../../back-up-restore-universes/) or creating [support bundles](../../troubleshoot/universe-issues/#use-support-bundles), can always be attempted even after such failures.

#### When to avoid retrying

For some failures, a retry of the task might continue to fail indefinitely. For example, an incorrect flag value might have been specified during a flag update operation. In such cases, run a new flag update operation with the corrected flag value instead of retrying the failed operation. You can fix the operation parameters instead of retrying for the following operations:

- [Flag updates](../edit-config-flags/)
- [Upgrade Linux Version](../upgrade-nodes/)
- [Update Kubernetes overrides](../edit-helm-overrides/)
- [Changing the instance configuration](../edit-universe/) of a Kubernetes universe

---
title: YugabyteDB Anywhere pre-checks
headerTitle: Automated pre-checks
linkTitle: Automated pre-checks
description: YugabyteDB Anywhere automated pre-checks for universe operations
headcontent: Validate universe health before rolling upgrades and cluster changes
menu:
  stable_yugabyte-platform:
    identifier: prechecks
    parent: manage-deployments
    weight: 80
type: docs
---

YugabyteDB Anywhere runs automated pre-checks before rolling upgrades and other cluster changes begin. The goal is to detect unhealthy nodes, unreachable management paths, or broken inter-node networking early, so rolling software upgrades and edit-cluster operations fail fast instead of stalling mid-task.

When a pre-check fails, the parent task is blocked in the pre-check phase. Task details and logs show which check failed and on which node. To view results, navigate to your universe **Tasks** tab, or refer to [Monitor and manage universe tasks](../retry-failed-task/).

Most checks can be turned off or tuned using universe-scoped [runtime configuration](../../administer-yugabyte-platform/manage-runtime-config/). Use that only when you understand the risk—for example, during a controlled maintenance window with manual validation.

## When pre-checks apply

Pre-checks apply to the following:

- Rolling upgrades, including software upgrades, rollbacks, certificate rotation, VM image upgrades, GFlag changes, and other upgrade tasks that use rolling upgrade mode.
- Edit cluster and universe operations on live nodes, including **Edit Universe**, **Replace Node**, and **Decommission Node**.
- Universe creation and node provisioning, including inter-node DB port connectivity checks when new nodes are added.

Many edit and upgrade dialogs include a **Run Pre-check Only** button. Use this to validate the planned change without applying it. The task appears in the universe **Tasks** list with a **Validation** prefix.

## Limitations

Comprehensive pre-checks described on this page do not apply in the following cases:

- Kubernetes universes.
- Non-rolling upgrades.
- Nodes not in **Live** state. For rolling upgrades, if any node in the restart set is not Live, comprehensive pre-checks are skipped.
- Task retries. Comprehensive pre-checks run only on the first attempt of a task; retries skip them to avoid duplicate work.

For additional pre-checks that apply during rolling operations—such as under-replicated tablets, leaderless tablets, and cluster consistency checks—refer to [Run pre-checks before edit and upgrade operations](../../troubleshoot/universe-issues/#run-pre-checks-before-edit-and-upgrade-operations).

## When comprehensive pre-checks run

Comprehensive pre-checks (service liveness and command execution) require the universe flag `yb.checks.comprehensive_prechecks.enabled` to be `true` (the default), and the task must be on its first attempt (not a retry). Additional requirements depend on the operation:

**Rolling upgrades**

- The upgrade option is **Rolling upgrade**, and there are nodes to restart.
- `skipNodeChecks` is `false` on the upgrade request.

**Edit universe, replace node, or decommission node**

- Pre-checks run on the live subset of nodes in the planned change (nodes that are Live in the task parameters). Non-live nodes are not included.

## Types of pre-checks

### Service liveness check

**Purpose:** Confirms YugabyteDB processes (and node agent, if present) respond on each node before YugabyteDB Anywhere restarts or reconfigures them.

**Scope:** YB-Master, YB-TServer, and node agent (when installed) on each node in the pre-check set.

**Typical failure conditions**

- YB-Master or YB-TServer process is down or not listening.
- Node agent is registered but not reachable.
- Liveness probe exceeds the configured timeout.

**Example error**

```text
Service(s) MASTER, TSERVER are not alive on node n1 (IP: 10.0.0.5)
```

**Remediation**

1. On the node (or via the YugabyteDB Anywhere UI), verify `yb-master`, `yb-tserver`, and node-agent services are running.
1. Review the universe **Nodes** tab and node logs; fix crash loops or port conflicts.
1. Retry the operation after processes are healthy.

**Disable**

Set `yb.checks.comprehensive_prechecks.enabled` to `false` on the universe.

### Node command execution check

**Purpose:** Verifies YugabyteDB Anywhere can run remote commands on the node via node agent (preferred) or SSH, before relying on that path for upgrade or edit scripts.

**Scope:** Each live node in edit, replace, or decommission operations; each node in the rolling restart set for upgrades; and stop-node when comprehensive pre-checks are enabled.

**Typical failure conditions**

- Dead node.
- Node agent unreachable or not executing commands.
- SSH keys, bastion, or firewall blocking management access.
- Command times out (default **10 seconds** per node).

**Example error**

```text
Cannot execute commands on node n1 (IP: 10.0.0.5). Node may be unreachable or SSH/node-agent connection may be unavailable.
```

**Remediation**

1. Confirm node agent health on the node (`node-agent` service) and that the node appears correctly in YugabyteDB Anywhere.
1. If using SSH fallback, verify provider credentials, security groups, and SSH access from YugabyteDB Anywhere to the node.
1. Fix networking or reinstall node agent if needed. Refer to [Prepare to upgrade YugabyteDB Anywhere](../../upgrade/prepare-to-upgrade/#node-agent).

**Disable**

Set `yb.checks.comprehensive_prechecks.enabled` to `false` on the universe.

### DB node port connectivity check

*(Internal task name: `CheckDbNodePortConnectivity`)*

**Purpose:** Validates TCP connectivity between database nodes on YB-Master RPC and YB-TServer RPC ports using a socket probe run from one node toward others. Checks both forward (source → target) and reverse (target → source) directions when IPs are available.

**Scope:** Universe **create**, **add node**, and related provisioning or configure flows—for nodes in eligible states, grouped by availability zone. Runs during universe creation and when adding or configuring nodes. It is not invoked by rolling upgrades or by edit-cluster operations on existing live nodes.

**Typical failure conditions**

- Security groups or firewall rules block RPC ports between availability zones or regions.
- Wrong private or public IP selection for cloud networking.
- Port filtered or unreachable (probe returns UNREACHABLE or FILTERED).

**Example error**

```text
Port connectivity check failed (forward) from n1 (10.0.0.5) to n2 (10.0.0.6) on port 7100
```

**Remediation**

1. Ensure the YB-Master and YB-TServer RPC ports (defaults: `7100` and `9100`; configurable per universe) are open between all database nodes in the universe, in both directions.
1. Confirm cloud private IP versus public IP settings match your network design.

**Disable**

Set `yb.checks.comprehensive_prechecks.enabled` to `false` on the universe. The same flag gates this check during provisioning.

### Data directory disk space check

**Purpose:** Ensures each targeted node has enough free space on the YugabyteDB data directory before running an operation that may write additional state to disk.

**Scope:** Runs as a pre-check for tasks that schedule it. Currently, this applies to the GFlags upgrade task.

**Typical failure conditions**

- Data directory filesystem nearly full.
- Wrong data directory path on the node.

**Example error**

```text
Node n1 has insufficient free disk space on data dir /mnt/d0: required 3221225472 bytes, available 1073741824 bytes
```

**Remediation**

1. Free disk space on the data volume or expand the disk.
1. Confirm `data_dir` and mount layout match universe configuration.
1. Retry the task that triggered the pre-check.

**Disable**

For upgrade tasks, set `skipNodeChecks: true` on the upgrade API request.

## Configuration

The following universe-scoped runtime configuration flags control comprehensive pre-checks:

| Flag | Details |
| :--- | :--- |
| `yb.checks.comprehensive_prechecks.enabled` | Default: `true`. When `false`, skips service liveness, command execution (comprehensive block), and DB port connectivity checks that are gated on this flag. |
| `yb.checks.comprehensive_prechecks.check_service_liveness_timeout` | Default: `1m`. Timeout for each liveness check subtask during comprehensive pre-checks. |

Refer to [Manage runtime configuration settings](../administer-yugabyte-platform/manage-runtime-config/) for instructions on viewing and changing these flags.

## Related topics

- [Monitor and manage universe tasks](../retry-failed-task/) — view pre-check task details and subtask progress.
- [Run pre-checks before edit and upgrade operations](../../troubleshoot/universe-issues/#run-pre-checks-before-edit-and-upgrade-operations) — additional rolling-operation checks and workarounds.
- [Prepare to upgrade YugabyteDB Anywhere](../../upgrade/prepare-to-upgrade/) — node agent requirements and installation.

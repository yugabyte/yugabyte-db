---
title: Reprovision universe nodes
headerTitle: Reprovision universe nodes
linkTitle: Reprovision nodes
description: Re-run node provisioning on existing universe nodes.
headcontent: Re-apply OS-level settings and reinstall node agent
menu:
  stable_yugabyte-platform:
    identifier: reprovision-nodes
    parent: manage-deployments
    weight: 12
type: docs
---

Starting in YugabyteDB Anywhere v2026.1.2.0, you can re-run node provisioning on an existing universe so OS-level settings match what the current YugabyteDB Anywhere expects.

Your data is preserved. Only the OS and node agent layers are reprovisioned; database software and data volumes remain untouched.

The action applies the following OS settings:

- Process and open-file limits (`nofile`, `nproc`)
- [Transparent hugepages](../../../deploy/manual-deployment/system-config/#enable-transparent-hugepages) (THP)
- Clock synchronization (chrony / ClockBound)
- Kernel/sysctl values (for example, `vm.swappiness`, `vm.max_map_count`, core dump pattern)

## When to reprovision

YugabyteDB Anywhere applies these settings when it provisions a node. These settings can change later if an operator updates a limit or sysctl manually, or if a configuration-management process overwrites a file or stops a service. If a change is incompatible with YugabyteDB, the node is misconfigured even though the database software and data remain unchanged.

Universe health checks report that drift. For example:

- A process-limit or open-file warning, when `nofile` or `nproc` has been lowered.
- A transparent hugepages warning, when THP is disabled or no longer matches the [required settings](../../prepare/server-nodes-software/#transparent-hugepages).
- A clock or NTP warning, when chrony is stopped or the system clock is no longer synchronized.
- A [YNP Version Skew](../../prepare/server-nodes-software/software-on-prem/#keep-provisioning-current) alert, when the node's provisioning version is behind YugabyteDB Anywhere. That check runs when `yb.node_agent.enable_ynp_version_check` is true.

Reprovisioning writes the current settings back and reinstalls node agent as part of provisioning. You can also run it proactively, before a health check reports a problem. After you upgrade YugabyteDB Anywhere, the OS settings it expects may have changed, and existing nodes can be out of date until you reprovision them.

In YugabyteDB Anywhere v2026.1.2.0 and later, **Actions > More > Reprovision Universe Nodes** is available for supported public cloud and on-premises universes whenever no other universe task is running, just like **Reinstall Node Agent**. Reinstall Node Agent reinstalls node agent. Reprovision Universe Nodes re-applies the OS settings above and reinstalls node agent as part of that provisioning.

Reprovisioning performs a rolling restart, the same as a node resize or VM image upgrade. Perform it during a low-traffic period and avoid scheduled backup windows.

{{< warning title="Replication factor less than 3" >}}
If the universe replication factor is less than 3, the universe is not available for reads and writes while nodes restart.
{{< /warning >}}

## Limitations

This action is not available for Kubernetes universes.

It is available for universes that YugabyteDB Anywhere provisions itself: public cloud, and on-premises universes with passwordless sudo.

| Deployment type | Support |
| :-------------- | :------ |
| [Public cloud](../../configure-yugabyte-platform/aws/) (AWS, GCP, Azure, OCI) | Full UI and API support. |
| On-premises with [legacy automatic provisioning](../../prepare/server-nodes-software/software-on-prem-auto/) | Full UI and API support. YugabyteDB Anywhere signs in and provisions the node. |
| On-premises, with legacy manual provisioning | Not supported. For universes with nodes configured using fully manual legacy provisioning, **Reprovision Universe Nodes** is not available, and the API returns an error. Re-apply OS settings by following the [rolling runbook](../../prepare/server-nodes-software/software-on-prem/#re-provision-nodes-of-an-existing-universe-non-sudo-on-premises). |
| On-premises, with node agent provisioning script | Not supported. The UI hides the action. The API returns an error. Re-apply OS settings using the [rolling runbook](../../prepare/server-nodes-software/software-on-prem/#re-provision-nodes-of-an-existing-universe-non-sudo-on-premises) (`node-agent-provision.sh`). |

This action does not replace [patching the Linux OS](../upgrade-nodes/) or replacing a boot disk. After a boot disk replacement, continue to follow that procedure, which reinstalls YugabyteDB software on the node.

## Reprovision nodes

To reprovision nodes, do the following:

1. Navigate to your universe.

1. Click **Actions > More > Reprovision Universe Nodes**.

1. Choose which nodes to reprovision:

    - **All nodes in this Universe** reprovisions every node, one at a time.
    - **Selected node** targets a single node. This option is unavailable for single-node universes.

1. Click **Reprovision Universe Nodes**.

YugabyteDB Anywhere starts a **ProvisionUniverseNodes** task. For each node, it does the following:

- Verifies the node is safe to take down.
- Stops YB-Master, YB-TServer, and YB Controller processes.
- Re-applies the OS settings listed above and reinstalls node agent.
- Starts the processes again, and waits for the server to be ready.

The task is rolling, [abortable, and retryable](../retry-failed-task/).

## Use the API

To reprovision all nodes:

```sh
curl '<platform-url>/api/v1/customers/<customer_uuid>/universes/<universe_uuid>/upgrade/provision_nodes' -X 'POST' -H 'X-AUTH-YW-API-TOKEN: <api-token>' -H 'Content-Type: application/json' -H 'Accept: application/json, text/plain, */*' \
--data-raw '{"nodeNames":[]}'
```

An empty `nodeNames` array means all nodes in the universe. To target a single node, pass that node's name:

```sh
curl '<platform-url>/api/v1/customers/<customer_uuid>/universes/<universe_uuid>/upgrade/provision_nodes' -X 'POST' -H 'X-AUTH-YW-API-TOKEN: <api-token>' -H 'Content-Type: application/json' -H 'Accept: application/json, text/plain, */*' \
--data-raw '{"nodeNames":["<node_name>"]}'
```

For information on creating an API token, refer to [API authentication](../../anywhere-automation/#authentication).

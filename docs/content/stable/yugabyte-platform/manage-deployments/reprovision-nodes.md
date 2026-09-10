---
title: Reprovision universe nodes
headerTitle: Reprovision universe nodes
linkTitle: Reprovision nodes
description: Re-run node provisioning on existing universe nodes using YNP.
headcontent: Re-apply OS-level settings and migrate nodes off legacy provisioning
menu:
  stable_yugabyte-platform:
    identifier: reprovision-nodes
    parent: manage-deployments
    weight: 12
type: docs
---

Starting in YugabyteDB Anywhere v2026.1.2.0, you can re-run node provisioning on an existing universe so OS-level settings match what the current YugabyteDB Anywhere expects. For universes still on [legacy provisioning](../../prepare/server-nodes-software/software-on-prem-legacy/), the action also migrates the nodes to use [automatic provisioning](../../prepare/server-nodes-software/software-on-prem/), including [node agent](/stable/faq/yugabyte-platform/#what-is-a-node-agent) and user-level systemd.

Your data is preserved. Only the OS and agent layers are reprovisioned; database software and data volumes remain untouched.

The action applies the following OS settings:

- Process and open-file limits (`nofile`, `nproc`)
- Transparent hugepages (THP)
- Clock synchronization (chrony / ClockBound)
- Kernel/sysctl values (for example, `vm.swappiness`, `vm.max_map_count`, core dump pattern)

## When to reprovision

YugabyteDB Anywhere surfaces when reprovisioning would help:

- A banner indicates that universe nodes need to migrate to node agent or off legacy provisioning.
- Health checks report OS-setting drift on a node. For example, a process-limit or open-file warning, THP flagged as misconfigured, or a clock/NTP synchronization issue.

The action is always available on supported universes (the same as **Reinstall Node Agent**). Run it when YugabyteDB Anywhere indicates one of the above, or when you want to apply current OS settings. This is not a prerequisite for upgrading to v2025.2; treat it like a patch to keep database nodes updated.

Reprovisioning performs a rolling restart, the same as a node resize or VM image upgrade. Perform it during a low-traffic period and avoid scheduled backup windows.

{{< warning title="Replication factor less than 3" >}}
If the universe replication factor is less than 3, the universe is not available for reads and writes while nodes restart.
{{< /warning >}}

## Limitations

This action is not available for Kubernetes universes.

| Deployment type | Support |
| :-------------- | :------ |
| Public cloud (AWS, GCP, Azure) | Full UI and API support. |
| On-premises (with passwordless sudo) | Full UI and API support. |
| On-premises, manual (no passwordless sudo) | Not supported. The UI hides the action. The API returns an error. Use the [`node-agent-provision.sh`](../../prepare/server-nodes-software/software-on-prem/) script instead. |

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
- [Provisions the nodes](../../prepare/server-nodes-software/software-on-prem/).
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

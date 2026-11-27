---
title: Automatic provisioning without privileged access
headerTitle: Automatic provisioning without privileged access
linkTitle: Non-privileged
description: Prepare on-premises database nodes when YugabyteDB Anywhere has no SSH access.
headContent: Prepare a VM when YugabyteDB Anywhere has no SSH access to it
menu:
  stable_yugabyte-platform:
    identifier: software-on-prem
    parent: on-prem-provisioning
    weight: 10
type: docs
rightNav:
  hideH4: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../software-on-prem-legacy/" class="nav-link">
      Choose access
    </a>
  </li>

  <li>
    <a href="../software-on-prem/" class="nav-link active">
      Non-privileged
    </a>
  </li>

  <li>
    <a href="../software-on-prem-auto/" class="nav-link">
      Privileged
    </a>
  </li>
</ul>

{{< note title="Non-privileged access" >}}
Use the following steps when YugabyteDB Anywhere has no SSH access to the provider VMs. In the provider configuration, leave **SSH Key Pairs** empty and turn **Manually Provision Nodes** on.

(This was formerly called automatic provisioning with the node agent script.)

On a provider with SSH keys and **Manually Provision Nodes** off, YugabyteDB Anywhere provisions the nodes itself. Run this script on a non-privileged provider. On a privileged provider the script stops and leaves the provider unchanged. See [Automatic provisioning with privileged access](../software-on-prem-auto/).
{{< /note >}}

You create the VMs. On each VM, run `node-agent-provision.sh` (also known as YNP, the YugabyteDB Node Provisioner) to prepare the node. The script can also create the on-premises provider and add the VM to the provider free pool.

## Overview

Provisioning turns a Linux VM into a database node.

With [privileged access](../software-on-prem-auto/), YugabyteDB Anywhere signs in and provisions the node. To [prepare the node yourself](../software-on-prem-manual/), you run a long list of host commands, on the order of 40 per node. That list changes as node requirements change, so automation you maintain yourself can drift.

Using the provisioning script, you can apply current node requirements without giving YugabyteDB Anywhere root SSH. The script is updated in sync with each release of YugabyteDB Anywhere, making it easier to stay current.

### What you get

- YugabyteDB Anywhere does not need SSH keys or sudo on the node.
- Root work and YugabyteDB Anywhere registration can be split across two teams. See [Split root and user privileges](#split-root-and-user-privileges-separation-of-duties).
- The same four steps replace the old per-node command list, including after a YugabyteDB Anywhere upgrade. See [Four steps at a glance](#four-steps-at-a-glance) and [Keep provisioning current](#keep-provisioning-current).
- The package you download matches the YugabyteDB Anywhere version, so the node gets the requirements for that release. See [What the script configures](#what-the-script-configures).
- The script checks that it can reach YugabyteDB Anywhere, then creates the provider and adds the node. See [Modify the configuration file](#modify-the-configuration-file).
- A preflight check runs as part of provisioning, and you can run it on its own. See [Preflight check](#preflight-check).
- A dry run prints the shell it would execute, which you can review or hand to another team. See [Dry run](#dry-run).
- A failed or partial run can be re-run. See [Run the provisioning script](#run-the-provisioning-script).
- Database services are user-level systemd units (`systemctl --user`), owned by `yugabyte`. See [What the script configures](#what-the-script-configures).

### Four steps at a glance

1. [Download the package](#download-the-package) that matches your YugabyteDB Anywhere version.
1. [Edit `node-agent-provision.yaml`](#modify-the-configuration-file).
1. [Run `node-agent-provision.sh`](#run-the-provisioning-script) as root, or split the run with `--root` and `--noroot`.
1. Create the universe. The node is already in the provider free pool. If the script did not create the provider, follow [Next steps](#next-steps).

## Prerequisites

- Provide one, three, five, or more VM(s) with the following installed:
  - [Supported Linux OS](../#linux-os)
  - [Additional software](../#additional-software)
  - If you are not connected to the Internet, [additional software for airgapped](../#additional-software-for-airgapped-deployment)

- YugabyteDB Anywhere is [installed and running](../../../install-yugabyte-platform/).

- VMs are accessible to YugabyteDB Anywhere over ports 9070 and 443. See [Networking requirements](../../networking/) for more information.

### Create data directories or mount points

Configure data directories or mount points for the node (typically `/data`). If you have multiple data drives, these might be for example `/mnt/d0`, `/mnt/d1`, and so on. The data drives must be accessible to the `yugabyte` user that will be created by the script.

## Prepare nodes for use in a database cluster

After you have created the VMs with the operating system and additional software, you must further prepare the VMs as follows:

1. Download the YugabyteDB Anywhere node agent package to the VM.
1. Modify the configuration file.
1. Run the provisioning script (as root or via sudo).

These steps prepare the node for use by YugabyteDB Anywhere, including setting ulimits and transparent hugepages. When YugabyteDB Anywhere is [installed and running](../../../install-yugabyte-platform/) and you set the [provider options](#run-the-provisioning-script), the script creates the [on-premises provider](../../../configure-yugabyte-platform/on-premises/) with **Manually Provision Nodes** on, or adds the node to the provider if it already exists and that option is on.

{{< tip title="Brownfield provisioning (Re-provision existing nodes)" >}}

For brownfield use cases where the nodes are already added to the node instances of the provider, you can generate config files. See [Generate configuration files](#generate-configuration-files).

{{< /tip >}}

Root or sudo privileges are only required to provision the nodes. After the node is provisioned (with [YugabyteDB Anywhere node agent](/stable/faq/yugabyte-platform/#what-is-a-node-agent) installed), sudo is no longer required.

For an existing universe whose nodes were prepared this way and lack transparent hugepages, or have them misconfigured, re-run the script. See [Re-provision nodes of an existing universe](#re-provision-nodes-of-an-existing-universe-non-sudo-on-premises). **Actions > Reprovision Universe Nodes** applies to [privileged access](../software-on-prem-auto/), where YugabyteDB Anywhere has passwordless sudo.

### Download the package

To begin, download the YugabyteDB Anywhere node agent package to the node you want to provision.

#### Use the API

If you have already installed YugabyteDB Anywhere and it is running, you can download the node agent package from YugabyteDB Anywhere using the [Download node agent API](https://api-docs.yugabyte.com/docs/yugabyte-platform/22174ba86f880-download-node-agent-installer-or-package).

```sh
curl -k https://<yba_address>/api/v1/node_agents/download\?downloadType\=package\&os\=LINUX\&arch\=AMD64 --fail --header 'X-AUTH-YW-API-TOKEN: <api_token>'  > node-agent.tar.gz
```

- `<yba_address>` is the address of your YugabyteDB Anywhere installation.
- `<api_token>` is an API token you created. For information on creating an API token, refer to [API authentication](../../../anywhere-automation/#authentication). The user creating the API token must have [Admin role](../../../administer-yugabyte-platform/anywhere-rbac/#built-in-roles) privileges or better.
- You can change the architecture from AMD64 to ARM64 as appropriate.

Use this method if you don't have internet connectivity. This downloads the same version of node agent as the version of YugabyteDB Anywhere you are running.

Extract the package and go to the `scripts` directory.

```sh
tar -xvzf node-agent.tar.gz && cd {{<yb-version version="stable" format="build">}}/scripts/
```

#### Direct download

Alternatively, obtain the node agent package from the YBA Installer package.

1. Download and extract the YBA Installer by entering the following commands:

    ```sh
    wget https://downloads.yugabyte.com/releases/{{<yb-version version="stable" format="long">}}/yba_installer_full-{{<yb-version version="stable" format="build">}}-linux-x86_64.tar.gz
    tar -xf yba_installer_full-{{<yb-version version="stable" format="build">}}-linux-x86_64.tar.gz
    cd yba_installer_full-{{<yb-version version="stable" format="build">}}/
    ```

1. Extract the yugabundle package:

    ```sh
    tar -xf yugabundle-{{<yb-version version="stable" format="build">}}-centos-x86_64.tar.gz
    cd yugabyte-{{<yb-version version="stable" format="build">}}/
    ```

1. Extract the node agent package and go to the `scripts` directory:

    ```sh
    tar -xf node_agent-{{<yb-version version="stable" format="build">}}-linux-amd64.tar.gz && cd {{<yb-version version="stable" format="build">}}/scripts/
    ```

    On ARM, run:

    ```sh
    tar -xf node_agent-{{<yb-version version="stable" format="build">}}-linux-arm64.tar.gz && cd {{<yb-version version="stable" format="build">}}/scripts/
    ```

### Modify the configuration file

Edit the `node-agent-provision.yaml` file in the scripts directory.

You can [review the file](https://github.com/yugabyte/yugabyte-db/blob/{{< yb-version version="stable" format="short">}}/managed/node-agent/resources/node-agent-provision.yaml) and its defaults on GitHub.

Set at least the following. Other fields in the sample have usable defaults.

- `ynp.node_ip`
- `yba.url`, `yba.customer_uuid`, `yba.api_key`
- `yba.node_name`, `yba.node_external_fqdn`

    `node_name` is a free-form label for the instance in YugabyteDB Anywhere. `node_external_fqdn` is the address YugabyteDB Anywhere uses to reach the node. You can set `node_name` to a hostname while `node_external_fqdn` is an IP address, or the other way around. Both the script and the manual add-instance flow accept that split.

- `yba.provider.name`, `yba.provider.region.name`, `yba.provider.region.zone.name`
- `yba.instance_type.name` and `yba.instance_type.mount_points`. For an existing instance type, `mount_points` must match that type. `cores`, `memory_size`, and `volume_size` are required when the instance type is new.

For complete list of options, see [Script details](#script-details).

Every run validates `node-agent-provision.yaml` first (including when run with `--generate_config`, `--dry_run`, and `--preflight_check` flags).

The sample `instance_type.cores`, `memory_size`, and `volume_size` values (`<number_of_cores>`, `<memory_in_gb>`, `<volume_in_gb>`) are not integers, so that check fails with `Failed to validate config`. Replace each with an integer, or delete the key when the instance type already exists.

### Run the provisioning script

Run the script as root. From a root shell:

```sh
./node-agent-provision.sh
```

Where sudo is allowed:

```sh
sudo ./node-agent-provision.sh
```

With no extra flag, this is a full provision: root modules and user modules. The script provisions the node, installs node agent, and runs preflight checks. For information on additional flags, see [Additional options](#additional-options).

If the `yba` fields are set, the script creates the provider with **Manually Provision Nodes** on, or adds the node when that provider already exists with the option on. If the option is off, the script stops and leaves the provider unchanged.

The script is idempotent. If a run stops partway through, run the same command again. The node agent directories and unit files stay in place and the re-run finishes the work. When the `yugabyte` user already exists, that user's home directory wins: the script replaces `ynp.yb_user_home` with it before the modules run. `--generate_config` writes the local user's home when the user exists, and the provider **YB Nodes Home Directory** when the user does not. Manual removal is for uninstalling YugabyteDB from a node you are taking out of service. See [Remove node agent](../../../administer-yugabyte-platform/uninstall-software/#delete-on-premises-database-server-nodes).

After the node is provisioned, reboot the node.

If the preflight check fails, rebooting the node may solve some issues (for example, incorrect ulimit settings). Then re-run the script.

## Verify provisioning

After running the script and rebooting the VM, you can verify that provisioning was successful and YugabyteDB Anywhere can communicate with the node by navigating to `https://<yugabytedbanywhere-host-ip>/nodeagent`, where `yugabytedbanywhere-host-ip` is the IP address hosting your YugabyteDB Anywhere instance.

The page lists the node agents that have been activated and their status.

## Additional options

<a id="split-team-provisioning"></a>

### Split root and user privileges (separation of duties)

Linux administrators often have root and no YugabyteDB context. Database administrators often have the YugabyteDB Anywhere API token and no root. The two flags split the script along that line. Run `--root` first. A later `--root` run leaves work that is already in the desired state in place (it does not create a second `yugabyte` user, and it re-applies settings that are already correct).

Available in v2026.1.0.0 and later. `--root` and `--noroot` are mutually exclusive. On v2025.2 and earlier, one operator runs the full script as root.

You can `su` to root for `--root`. A `sudo` prefix is only needed when sudo is the mechanism you use.

#### New universes

1. Database administrators download the node agent package from the target YugabyteDB Anywhere version and edit `node-agent-provision.yaml`. Replace `yba.instance_type.cores`, `memory_size`, and `volume_size` with integers, or delete those keys. The sample placeholders fail validation before the script writes a command list. Set `yba.url`, `yba.api_key`, and the node FQDN or IP (`yba.node_external_fqdn`).

1. Linux administrators create the VM, install Linux and the [additional software](../#additional-software), and open the [required ports](../../networking/). They run a root dry run, which writes the root command list and does not change the node, then either run the rendered root script or run `--root`:

    ```sh
    sudo ./node-agent-provision.sh --root --dry_run
    sudo ./node-agent-provision.sh --root
    ```

    `--root` runs only modules that need elevated privileges (the `yugabyte` user, chrony, THP, ulimits, sysctl, sudoers, node exporter, and root systemd units).

1. Database administrators run `--noroot` as `yugabyte`:

    ```sh
    ./node-agent-provision.sh --noroot
    ```

    `--noroot` must run as `yugabyte`. It installs user-level systemd units and cgroups, installs node agent, and registers the node. The node then shows up in the provider free pool.

#### Existing universes

Generate the YAML with `--generate_config` (see [Generate configuration files](#generate-configuration-files)), then follow [Re-provision nodes of an existing universe](#re-provision-nodes-of-an-existing-universe-non-sudo-on-premises). The Linux team runs `--root` during the maintenance window. The database team runs `--noroot`.

### Preflight check

Available in v2024.2 and later. For troubleshooting, you can run the script's preflight checks separately:

```sh
sudo ./node-agent-provision.sh --preflight_check
```

From a root shell, omit `sudo`.

Use the `--preflight_check_out_file` flag (v2025.2.4.0 and later) to specify the file path for the preflight_check output. Only the check JSON output is saved. You can read and parse the files for automation.

Provisioning also runs this check. On a node that is already in a universe, run it only while the node is in maintenance mode. The on-premises checks bind the database ports, so they cannot run while YB-Master and YB-TServer are listening. See [Re-provision nodes of an existing universe](#re-provision-nodes-of-an-existing-universe-non-sudo-on-premises).

### Dry run

Available in v2025.1 and later. `--dry_run` renders the shell the script would run and does not change the node. It writes two files under `ynp.tmp_directory` (default `/tmp`):

- **Install script** (`*_run`). The commands that would provision the node.
- **Precheck script** (`*_precheck`). The checks that would run afterward.

The command prints each path twice: once as `Install Script:` or `Precheck Script:`, and again from the provisioning log. They are the same two files. Open the paths on the `Install Script:` and `Precheck Script:` lines.

Replace the sample `instance_type.cores`, `memory_size`, and `volume_size` placeholders with integers, or delete those keys. The script validates the file before it writes the scripts.

```sh
./node-agent-provision.sh --dry_run
```
Combine it with the privilege flags (v2026.1.0.0 and later):

```sh
sudo ./node-agent-provision.sh --root --dry_run
./node-agent-provision.sh --noroot --dry_run
```

Typical uses:

- Read the commands before you run them.
- Hand the `--root --dry_run` output to the Linux team. That is the root half of [Split root and user privileges](#split-root-and-user-privileges-separation-of-duties).
- Feed the rendered script into your own automation.
- Audit what a sudo whitelist would need to allow. See [sudo whitelist](#sudo-whitelist).

{{< note title="Each path is logged twice" >}}
The console line and the provisioning log both print the temp path, so a two-script dry run shows four paths. Use the `Install Script:` and `Precheck Script:` lines. The other two lines name the same files.
{{< /note >}}

<a id="per-node-overrides"></a>

### Override configuration

Using the `--config_override` flag (available in v2025.2.4.0 and later), you can keep one `node-agent-provision.yaml` for a fleet (YugabyteDB Anywhere URL, API token, chrony servers, home directories, provider defaults) and pass only the fields that differ per VM.

You can pass as many overrides as needed. The value after `=` is JSON, and it must match that field's type: a quoted string, a number, `true` or `false`, or a JSON array. Nested fields use dotted paths, such as `yba.instance_type.name`.

#### One configuration file, many nodes

Copy the same file to every node. Override the three fields that identify the node. For example:

```sh
./node-agent-provision.sh \
    --config_override ynp.node_ip=\"10.1.2.3\" \
    --config_override yba.node_name=\"db-node-03\" \
    --config_override yba.node_external_fqdn=\"db-node-03.example.com\"
```

`yba.node_name` is the free-form label. `yba.node_external_fqdn` is the address YugabyteDB Anywhere reaches. `ynp.node_ip` is the address other database nodes use.

#### Other overrides

Override chrony servers (a list of strings):

```sh
./node-agent-provision.sh --config_override ynp.chrony_servers=[\"s1\",\"s2\"]
```

Override the FQDN and the YugabyteDB Anywhere URL together:

```sh
./node-agent-provision.sh --config_override yba.node_external_fqdn=\"my-new-fqdn\" --config_override yba.url=\"https://new-yba-url.com\"
```

### Generate configuration files

Use the `--generate_config` flag (v2025.2.4.0 and later) to write a YAML file from the node's current provider registration. The command does not provision the node. This is how you hydrate a configuration for a node that is already in the provider, before [re-provisioning an existing universe](#re-provision-nodes-of-an-existing-universe-non-sudo-on-premises).

The seed `node-agent-provision.yaml` needs `yba.url`, `yba.api_key`, and the node FQDN or IP (`yba.node_external_fqdn`). Replace `yba.instance_type.cores`, `memory_size`, and `volume_size` with integers, or delete those keys. The sample placeholders fail validation, and the script checks the seed before it writes a file. The generated file is named `node-agent-provision-generated.yaml` in the same directory.

```sh
./node-agent-provision.sh --generate_config
```

`--config_file` (default `./node-agent-provision.yaml`, available in v2024.2 and later) selects which file a later run reads. Provision from the generated file as root. On a node that is already in a universe, run that command only during the maintenance window in [Re-provision nodes of an existing universe](#re-provision-nodes-of-an-existing-universe-non-sudo-on-premises). The checks bind the database ports.

```sh
sudo ./node-agent-provision.sh --config_file node-agent-provision-generated.yaml
```

From a root shell, omit `sudo`.

When you combine [--config_override](#per-node-overrides) with `--generate_config`, overrides apply to fields used to contact YugabyteDB Anywhere (for example, `yba.url`), not to values that YugabyteDB Anywhere returns into the generated file.

Use `--generate_and_run` (v2025.2.4.0 and later) to generate the file and provision in one invocation. The script switches to the generated file for the provision. Run it as root, and on a universe node only while that node is in maintenance mode.

```sh
sudo ./node-agent-provision.sh --generate_and_run
```

To render scripts from the generated file without changing the node:

```sh
./node-agent-provision.sh --generate_and_run --dry_run
```

## Re-provision nodes of an existing universe (non-sudo on-premises)

Use this when the universe already exists, **Manually Provision Nodes** is on, and you want every node prepared by `node-agent-provision.sh`. YugabyteDB Anywhere has no task for this path, because that task needs sudo on the node. Your automation owns the loop. **Enter Maintenance Mode** in the UI is the **Stop Node** API (`nodeAction` `STOP`). **Exit Maintenance Mode** is **Start Node** (`START`).

If **Manually Provision Nodes** is off, use [Reprovision universe nodes](../../../manage-deployments/reprovision-nodes/) instead.

Do this one node at a time. On a universe with replication factor 3 or more, the universe stays available for reads and writes. Stopping a second node while the first is still in maintenance mode makes tablets unavailable.

{{< warning title="Maintenance window" >}}
If the node stays in maintenance mode longer than 15 minutes, the Raft leader treats the follower as failed and re-replicates its data. The timeout is [follower_unavailable_considered_failed_sec](../../../../reference/configuration/yb-tserver/#follower-unavailable-considered-failed-sec) (default 900 seconds). Raise that flag on YB-TServer and YB-Master before a slow run, and set [log_min_seconds_to_retain](../../../../reference/configuration/yb-tserver/#log-min-seconds-to-retain) to the same value so the WAL is kept until the node returns. Change the flags in [Edit configuration flags](../../../manage-deployments/edit-config-flags/). See [Enter maintenance mode](../../../manage-deployments/remove-nodes/#enter-and-exit-maintenance-mode).
{{< /warning >}}

### On every node, before you stop anything

1. [Download the node agent package](#download-the-package) that matches the YugabyteDB Anywhere version.
1. In `node-agent-provision.yaml`, set `yba.url`, `yba.api_key`, and the node FQDN or IP (`yba.node_external_fqdn`). Replace `yba.instance_type.cores`, `memory_size`, and `volume_size` with integers, or delete those keys. The sample values `<number_of_cores>`, `<memory_in_gb>`, and `<volume_in_gb>` fail validation, and `--generate_config` does not run until they are replaced or removed.
1. Hydrate the rest from YugabyteDB Anywhere (v2025.2.4.0 and later):

    ```sh
    ./node-agent-provision.sh --generate_config
    ```

    This writes `node-agent-provision-generated.yaml`.

1. Optionally, review the commands before the maintenance window. `--dry_run` makes no changes (v2025.1 and later). On v2026.1.0.0 and later, a root dry run is the hand-off to the Linux team:

    ```sh
    ./node-agent-provision.sh --config_file node-agent-provision-generated.yaml --dry_run
    sudo ./node-agent-provision.sh --config_file node-agent-provision-generated.yaml --root --dry_run
    ```

### On each node, rolling

1. Put the node in maintenance mode.

    In YugabyteDB Anywhere, open the universe **Nodes** tab, click the node **Actions**, and choose **Enter Maintenance Mode**. Wait until that task succeeds before you run the script.

    With the API:

    ```sh
    curl '<platform-url>/api/v1/customers/<customer_uuid>/universes/<universe_uuid>/nodes/<node_name>' -X 'PUT' -H 'X-AUTH-YW-API-TOKEN: <api-token>' -H 'Content-Type: application/json' -H 'Accept: application/json, text/plain, */*' \
    --data-raw '{"nodeAction":"STOP"}'
    ```

    A successful PUT returns a task UUID (`taskUUID`). The PUT itself fails only when the remaining live nodes would fall below quorum. Under-replication and whether the node is safe to take down run inside the task, after that response. Poll the task and run the script only when `status` is `Success`:

    ```sh
    curl '<platform-url>/api/v1/customers/<customer_uuid>/tasks/<task_uuid>' -H 'X-AUTH-YW-API-TOKEN: <api-token>'
    ```

    On `Failure` or `Aborted`, the node is still up. Wait until the universe is healthy and try this node again.

    Maintenance mode is required because the preflight checks bind the database ports. Against a live node those checks collide with YB-Master and YB-TServer.

1. Provision the node from the generated file.

    On v2026.1.0.0 and later, the Linux team runs the root modules, then the database team runs the user modules as `yugabyte`:

    ```sh
    sudo ./node-agent-provision.sh --config_file node-agent-provision-generated.yaml --root
    ./node-agent-provision.sh --config_file node-agent-provision-generated.yaml --noroot
    ```

    When one operator holds both privileges, run the full script as root and omit `--root` and `--noroot`. That is also the command on v2025.2 (v2025.2.4.0 and later for `--generate_config`):

    ```sh
    sudo ./node-agent-provision.sh --config_file node-agent-provision-generated.yaml
    ```

    From a root shell, omit `sudo`. If the command fails, run it again. See [Run the provisioning script](#run-the-provisioning-script).

1. Start the node.

    In YugabyteDB Anywhere, on the **Nodes** tab, click **Actions > Exit Maintenance Mode**. Wait until that task succeeds before you continue to the next node.

    With the API:

    ```sh
    curl '<platform-url>/api/v1/customers/<customer_uuid>/universes/<universe_uuid>/nodes/<node_name>' -X 'PUT' -H 'X-AUTH-YW-API-TOKEN: <api-token>' -H 'Content-Type: application/json' -H 'Accept: application/json, text/plain, */*' \
    --data-raw '{"nodeAction":"START"}'
    ```

    A successful PUT returns a task UUID. Poll `GET /api/v1/customers/<customer_uuid>/tasks/<task_uuid>` until `status` is `Success` before you continue to the next node.

1. Confirm this node's agent is registered at `https://<yba>/nodeagent`. Confirm the provider and its instance list are unchanged, aside from agent metadata. Master placement can move: stopping a master node can start a replacement master on another node in the same zone (`yb.start_master_on_stop_node`, default `true`), and starting the node does not move that master back. See [Enter maintenance mode](../../../manage-deployments/remove-nodes/#enter-and-exit-maintenance-mode).
1. Continue with the next node.

### Adding a script-provisioned node to a legacy universe

A node you provision with the script joins the provider and zone you name in the YAML. The universe can run that way. Use one method for the whole universe: keep preparing new nodes the way the existing nodes were prepared, or re-provision every node with this runbook before you add more. Mixing legacy-manual nodes and script-provisioned nodes in one universe is discouraged.

## Keep provisioning current

After you upgrade YugabyteDB Anywhere, download the matching node agent package and re-run the script when node-level requirements have changed. Re-running is recommended. It is not mandatory on an upgrade that did not change those requirements.

The package carries a `ynp_version`. After the provision and precheck scripts run, the script writes that version under the `yugabyte` user's home in `.yugabyte/`, including when either script failed (v2025.2.3.0 and later). The value changes when node-level requirements change, not on every YugabyteDB Anywhere build.

YugabyteDB Anywhere can flag a mismatch:

- **YNP Version Skew** warns when the version in that file is behind YugabyteDB Anywhere, or the file is missing. The check runs only when `yb.node_agent.enable_ynp_version_check` is true. A failed re-provision still writes the package version, so the alert can clear while the node is missing the new requirements. Confirm the script exited successfully.
- **Incorrect THP settings** warns when THP does not match the [required settings](../#transparent-hugepages).
- **THP Issue Threshold Reached** fires when the node is already under memory pressure and TServer RSS is higher than TCMalloc accounts for.

How you clear the drift depends on the provider toggle. When **Manually Provision Nodes** is off, use [Reprovision universe nodes](../../../manage-deployments/reprovision-nodes/). When it is on, use [Re-provision nodes of an existing universe](#re-provision-nodes-of-an-existing-universe-non-sudo-on-premises).

### Runtime configuration

Both keys are global, and default to `false`. A global key applies to every provider and every universe. Only a Super Admin can change global parameters. See [Manage runtime configuration settings](../../../administer-yugabyte-platform/manage-runtime-config/).

| Key | Effect |
| :--- | :--- |
| `yb.node_agent.enable_ynp_version_check` | When `true`, adding a node requires the node's YNP major version to match YugabyteDB Anywhere, and health checks raise **YNP Version Skew** alert on a mismatch or a missing version file. There is no provider or universe override. |
| `yb.node_agent.disable_ynp_node_preflight_check` | When `true`, YugabyteDB Anywhere runs the legacy `preflight_checks.sh` instead of the YNP preflight checks while it adds or validates a manual on-premises node. A preflight still runs. The checks inside `node-agent-provision.sh` are unchanged. |

## Script details

### What the script configures

The package you download matches the YugabyteDB Anywhere version you are running. Re-running it is how a node picks up requirements that were added after the node was first prepared. See [Keep provisioning current](#keep-provisioning-current).

| What | Privilege | Added or changed |
| :--- | :--- | :--- |
| `yugabyte` user | Root (`--root`) | v2024.2 |
| ulimits and sysctl (`vm.swappiness`, `vm.max_map_count`, core pattern) | Root | v2024.2 |
| Transparent hugepages | Root | v2024.2. Required settings were updated in May 2025. A wrong setting raises **Incorrect THP settings**. **THP Issue Threshold Reached** fires when the node is under memory pressure and RSS is higher than TCMalloc accounts for. |
| chrony | Root | v2024.2 |
| ClockBound | Root. Optional (`is_configure_clockbound`, default false). Requires chrony. | v2025.2.3.0 |
| Node exporter | Root. Installs the bundled node exporter and restarts `node_exporter.service` on `node_exporter_port` (default 9300). An exporter already running under that unit is replaced with the bundled binary, then restarted. | v2024.2 |
| cgroups | User (`--noroot`). Controlled by `configure_cgroup` (default true). | v2024.2 |
| User-level systemd | User. Default (`use_system_level_systemd` false). | Cron-based universes are not supported in v2025.2 and later. |
| Node agent | User. Installed as `yugabyte` under user-level systemd. | v2024.2. Running as `yugabyte` rather than root is the v2025.2 behavior. |
| Data directories | You create the mount points before the script runs. The script checks that `yugabyte` can use them. | v2024.2 |
| Provider, instance type, and node registration | User. Runs when the [yba](#yba) fields in the script are set. | v2024.2 |

### Script parameters

You can [review the file](https://github.com/yugabyte/yugabyte-db/blob/{{< yb-version version="stable" format="short">}}/managed/node-agent/resources/node-agent-provision.yaml) and its defaults on GitHub. `--config_override` addresses the same fields with dotted paths (`ynp.node_ip`, `yba.url`).

#### `ynp`

| Option | Required | Value |
| :--- | :--- | :--- |
| `chrony_servers` | Default `[]` | Addresses of your NTP servers. Set this when the node does not already use those servers. |
| `yb_home_dir` | Default `/home/yugabyte` | Directory where YugabyteDB is installed. |
| `yb_user_home` | Default `yb_home_dir` | Home directory used when the script creates the `yugabyte` user. When that user already exists, the script uses the existing home and ignores this value. v2025.2.4.0 and later. |
| `yb_user_id` | Default `1004` | UID for a `yugabyte` user the script creates. Use the same UID on every node. Ignored when the user already exists. |
| `no_proxy_list` | Default `[]` | Hosts and addresses to exclude from an HTTP proxy. |
| `is_airgap` | Default `false` | Set to `true` for an airgapped node. |
| `use_system_level_systemd` | Default `false` | `false` uses user-level systemd. The sample file omits this key; the schema accepts it. |
| `node_ip` | Required | IP address other nodes use to reach this node. Replace the sample `127.0.0.1`. |
| `tmp_directory` | Default `/tmp` | Directory for temporary files during provisioning. Dry-run scripts are written in this directory. |
| `node_agent_port` | Default `9070` | Node agent listen port. Must match the YugabyteDB Anywhere runtime configuration, and must be the same on every node. |
| `node_exporter_port` | Default `9300` | Listen port for node exporter. When the script creates the provider, it registers this port. `--generate_config` copies the provider's node exporter port into this key. If the provider already exists and its port differs from this value, the script stops. |
| `is_configure_clockbound` | Default `false` | Set to `true` to install [ClockBound](https://github.com/aws/clock-bound) and point the provider at it, so universes created from the provider set [time_source](../../../../reference/configuration/yb-master/#time-source) to `clockbound`. ClockBound requires chrony. v2025.2.3.0 and later. |
| `configure_cgroup` | Default `true` | When `true`, the script configures TServer cgroup isolation. When `false`, it skips cgroup tuning. |

#### `yba`

Set these so the script can create the [on-premises provider](../../../configure-yugabyte-platform/on-premises-provider/) and add the node. YugabyteDB Anywhere must be installed and running.

| Option | Required | Value |
| :--- | :--- | :--- |
| `url` | Required | Base URL of YugabyteDB Anywhere, for example `https://yba.example.com`. |
| `skip_tls_verify` | Default `true` | Skip TLS certificate verification when calling YugabyteDB Anywhere. |
| `certificate_name` | Default empty | Label of a certificate from **Integrations > Security > Encryption in Transit > Certificates**. When empty, YugabyteDB Anywhere generates a self-signed root CA for node agent. |
| `customer_uuid` | Required | Customer ID. In YugabyteDB Anywhere, click the **Profile** icon and choose **User Profile**. |
| `api_key` | Required | API token from **User Profile > Generate Key**. The user needs the [Admin role](../../../administer-yugabyte-platform/anywhere-rbac/#built-in-roles) or better. |
| `node_name` | Required | Free-form instance label. This is the name you see in YugabyteDB Anywhere. It does not have to be an IP address or a DNS name. |
| `node_external_fqdn` | Required | FQDN or IP address of the node, reachable from the YugabyteDB Anywhere server. |
| `provider.name` | Required | Provider to create, or the existing provider that should receive the node. |
| `provider.region.name` | Required | Region name, for example `us-west-1`. |
| `provider.region.zone.name` | Required | Zone name, for example `us-west-1a`. |
| `provider.region.latitude` | Default `360` | Region latitude. Replace the placeholder when you want a real coordinate. |
| `provider.region.longitude` | Default `360` | Region longitude. Replace the placeholder when you want a real coordinate. |
| `instance_type.name` | Required | Instance type name. For a new type, this is the name YugabyteDB Anywhere stores (for example `c5.large`). |
| `instance_type.cores` | Required for a new instance type | Number of cores. Replace the sample `<number_of_cores>` with an integer, or delete the key when the instance type already exists. |
| `instance_type.memory_size` | Required for a new instance type | Memory in GB. Replace the sample `<memory_in_gb>` with an integer, or delete the key when the instance type already exists. |
| `instance_type.volume_size` | Required for a new instance type | Storage volume in GB. Replace the sample `<volume_in_gb>` with an integer, or delete the key when the instance type already exists. |
| `instance_type.mount_points` | Required | Data mount points you created before running the script. For an existing instance type, the list must match the mount paths of that instance type in YugabyteDB Anywhere. |

If the provider does not exist, the script creates it and turns **Manually Provision Nodes** on. If it exists with that option on, the script adds the node. If it exists with the option off, the script refuses the provider.

#### `logging`

| Option | Required | Value |
| :--- | :--- | :--- |
| `level` | Default `INFO` | `DEBUG`, `INFO`, `WARNING`, `ERROR`, or `CRITICAL`. |
| `directory` | Default `./logs` | Directory for the provisioning log. |
| `file` | Default `app.log` | Provisioning log file name. |

## sudo whitelist

You need a sudo whitelist only when sudo is how you run the script. A root shell (`su` to root, then `./node-agent-provision.sh` with no sudo prefix) does not use the whitelist.

If security restrictions require you to explicitly list the commands that you'll be running as root under sudo, you can add the following commands to the sudo whitelist:

```sh
sudo ./node-agent-provision.sh --preflight_check
sudo ./node-agent-provision.sh
```

The underlying fine-grained commands that the script runs during provisioning depend on the version of YugabyteDB Anywhere, and are updated as newer capabilities are incorporated.

On packages that still log rendered templates during `--preflight_check`, the same files show up in the log:

```sh
sudo ./node-agent-provision.sh --preflight_check 2>&1 | grep "INFO - /tmp/tmp.*$"
```

```output
2025-02-20 23:01:37,290 - commands.provision_command - INFO - /tmp/tmp0ey61a1c
2025-02-20 23:01:37,290 - commands.provision_command - INFO - /tmp/tmppri1g4r_
```

The first path is the precheck script. The second is the install script. On current packages, prefer the labeled `Install Script:` and `Precheck Script:` lines from `--dry_run`. Each path may be printed twice. See [Dry run](#dry-run).

## Next steps

If you did not provide details for the provider configuration, you will need to do the following:

1. If the on-premises provider has not been created, create one.

    Refer to [Create the provider configuration](../../../configure-yugabyte-platform/on-premises-provider/).

1. Add the node to the provider.

    Refer to [Add nodes to the on-premises provider](../../../configure-yugabyte-platform/on-premises-nodes/).

When you add a node to a universe that was prepared another way, keep a single provisioning method for that universe. See [Adding a script-provisioned node to a legacy universe](#adding-a-script-provisioned-node-to-a-legacy-universe).

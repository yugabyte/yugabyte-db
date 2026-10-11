---
title: Requirements for servers for database nodes using on-premises providers
headerTitle: Automatically provision database nodes for on-premises providers
linkTitle: On-premises provider
description: Prepare a VM for deploying universes on-premises.
headContent: Prepare a VM for deploying universes on-premises
menu:
  stable_yugabyte-platform:
    identifier: software-on-prem
    parent: server-nodes-software
    weight: 20
type: docs
rightNav:
  hideH4: true
---

When deploying universes using an on-premises provider, YugabyteDB Anywhere relies on you to manually create the VMs for the database nodes and provide these pre-created VMs to YugabyteDB Anywhere.

Using the `node-agent-provision.sh` script, also known as YNP (YugabyteDB Node Provisioner), you can provision a VM, create the on-premises provider, and add the VM to the provider free pool. YugabyteDB Anywhere does not need an SSH key for that node.

## When to use the provisioning script

Check the on-premises provider's **Manually Provision Nodes** toggle (**Integrations > Infrastructure > On-Premises Datacenters**, then **Advanced**). This control indicates whether YugabyteDB Anywhere holds passwordless-sudo SSH keys for the nodes.

| Manually Provision Nodes | What to do |
| :--- | :--- |
| **Off**<br>YugabyteDB Anywhere has passwordless sudo over SSH. | YugabyteDB Anywhere provisions the nodes.<br>Running `node-agent-provision.sh` on these database nodes is _unsupported_.<br>To re-apply OS settings, use **Actions > More > Reprovision Universe Nodes** (v2026.1.2.0 and later). See [Reprovision universe nodes](../../../manage-deployments/reprovision-nodes/). |
| **On**<br>YugabyteDB Anywhere has no sudo SSH key. | Use this procedure. The script turns **Manually Provision Nodes** on when it creates the provider. An existing provider with the toggle off is refused (`Provider is configured for sudo provisioning, which is not supported`). |

To compare this script with legacy manual, legacy automatic, and assisted provisioning, see [Choose a provisioning method](#choose-a-provisioning-method).

## Overview

### What provisioning does

Provisioning turns a Linux VM into a database node. It has two privilege levels, matching `--root` and `--noroot` (v2026.1.0.0 and later):

- **Root.** Create the `yugabyte` user, set ulimits and sysctl, configure transparent hugepages (THP) and chrony, and install node exporter. ClockBound is optional.
- **User (`yugabyte`).** Configure cgroups and user-level systemd, install node agent, check data directories, and (when the YugabyteDB Anywhere fields are set) register the node with the provider.

For the full list, including the release that added each item, see [What the script configures](#what-the-script-configures).

### Why the script exists

Legacy on-premises provisioning asked either YugabyteDB Anywhere or a DBA to run a long list of host commands (on the order of 40 per node). That list changed as node requirements changed: THP, chrony, ClockBound, a node agent running as the `yugabyte` user, and user-level systemd in place of cron. Customers who encoded the list in their own automation drifted. A missed step showed up later as a preflight failure. In the field, that drift has shown up as out-of-memory incidents, clock skew, and database services left running as root. Nodes also missed later improvements such as ClockBound.

The script is the supported way to apply the current node requirements without giving YugabyteDB Anywhere root SSH.

### What you get

- YugabyteDB Anywhere does not need SSH keys or sudo on the node. See [When to use the provisioning script](#when-to-use-the-provisioning-script).
- Root work and YugabyteDB Anywhere registration can be split across two teams. See [Split root and user privileges](#split-root-and-user-privileges-separation-of-duties).
- The same four steps replace the old per-node command list, including after a YugabyteDB Anywhere upgrade. See [Four steps at a glance](#four-steps-at-a-glance) and [Keep provisioning current](#keep-provisioning-current).
- The package you download matches the YugabyteDB Anywhere version, so the node gets the requirements for that release. See [What the script configures](#what-the-script-configures).
- The script checks that it can reach YugabyteDB Anywhere, then creates or updates the provider and adds the node. See [Modify the configuration file](#modify-the-configuration-file).
- A preflight check runs as part of provisioning, and you can run it on its own. See [Preflight check](#preflight-check).
- A dry run prints the shell it would execute, which you can review or hand to another team. See [Dry run](#dry-run).
- A failed or partial run can be re-run. See [Run the provisioning script](#run-the-provisioning-script).
- Database services are user-level systemd units (`systemctl --user`), owned by `yugabyte`. See [What the script configures](#what-the-script-configures).

### Four steps at a glance

1. [Download the package](#download-the-package) that matches your YugabyteDB Anywhere version.
1. [Edit `node-agent-provision.yaml`](#modify-the-configuration-file).
1. [Run `node-agent-provision.sh`](#run-the-provisioning-script) as root, or split the run with `--root` and `--noroot`.
1. Create the universe. The node is already in the provider free pool. If the script did not create the provider, follow [Next steps](#next-steps).

### Choose a provisioning method

This table lists each on-premises provisioning method, who it is for, and whether it is supported.

| Method | How it works | Who it is for | Status | Manually Provision Nodes |
| :--- | :--- | :--- | :--- | :--- |
| Legacy manual | You prepare each node yourself (often with your own automation) and add it to the provider. | Teams that already own node preparation and do not give YugabyteDB Anywhere SSH. | Supported. Deprecated in v2024.2. Prechecks now enforce the [documented node requirements](../), so a homegrown script has to match that list. | On |
| Node agent provisioning script (`node-agent-provision.sh`, YNP) | You run the script. YugabyteDB Anywhere has no SSH key for the node. | New on-premises nodes, and existing non-sudo universes you are moving off legacy manual. | Supported. Recommended as of v2024.2. | On. The script sets this. |
| Legacy automatic | YugabyteDB Anywhere holds passwordless-sudo SSH keys and provisions the node itself, using the same provisioning mechanism as the script. | Providers where YugabyteDB Anywhere already has sudo SSH. | Supported. Deprecated in v2024.2. Running the script by hand on these nodes is unsupported. Re-apply settings with [Reprovision Universe Nodes](../../../manage-deployments/reprovision-nodes/) (v2026.1.2.0 and later). | Off |
| Legacy assisted (`provision_instance.py`) | You ran a script from the **Instances** page and typed a sudo password for each action. | Historical. | Removed in v2025.2. | On, with password sudo rather than passwordless sudo. |

For a new node, use the node agent provisioning script when YugabyteDB Anywhere does not have passwordless sudo.

If **Manually Provision Nodes** is off in the provider, leave provisioning to YugabyteDB Anywhere. Do not run the script.

## Prerequisites

- Provide one, three, five, or more VM(s) with the following installed:
  - [Supported Linux OS](../#linux-os)
  - [Additional software](../#additional-software)
  - If you are not connected to the Internet, [additional software for airgapped](../#additional-software-for-airgapped-deployment)

- YugabyteDB Anywhere is [installed and running](../../../install-yugabyte-platform/).

- VMs are accessible to YugabyteDB Anywhere over ports 9070 and 443. See [Networking requirements](../../networking/) for more information.

### Create data directories or mount points

Configure data directories or mount points for the node (typically `/data`). If you have multiple data drives, these might be for example `/mnt/d0`, `/mnt/d1`, and so on. The data drives must be accessible to the `yugabyte` user that will be created by the script.

## How to prepare the nodes for use in a database cluster

After you have created the VMs with the operating system and additional software, you must further prepare the VMs as follows:

1. Download the YugabyteDB Anywhere node agent package to the VM.
1. Modify the configuration file.
1. Run the provisioning script (as root, or from a root shell).

For a universe that already exists and was prepared without giving YugabyteDB Anywhere sudo, follow [Re-provision nodes of an existing universe](#re-provision-nodes-of-an-existing-universe-non-sudo-on-premises) instead of this greenfield flow.

Root privileges are only required to provision the nodes. After the node is provisioned (with [YugabyteDB Anywhere node agent](/stable/faq/yugabyte-platform/#what-is-a-node-agent) installed), sudo is no longer required.

Where your policy forbids a `sudo` prefix on a command, `su` to root and run the script from that shell. The [sudo whitelist](#sudo-whitelist) applies only when sudo is how you run the script.

For existing universes (v2026.1.2.0 and later) where YugabyteDB Anywhere has passwordless sudo, re-apply provisioning from the UI. See [Reprovision universe nodes](../../../manage-deployments/reprovision-nodes/).

### What the script configures

The package you download matches the YugabyteDB Anywhere version you are running. Re-running it is how a node picks up requirements that were added after the node was first prepared. See [Keep provisioning current](#keep-provisioning-current).

| What | Privilege | Added or changed |
| :--- | :--- | :--- |
| `yugabyte` user | Root (`--root`) | v2024.2 |
| ulimits and sysctl (`vm.swappiness`, `vm.max_map_count`, core pattern) | Root | v2024.2 |
| Transparent hugepages | Root | v2024.2. Required settings were updated in May 2025. A wrong setting raises **Incorrect THP settings**. **THP Issue Threshold Reached** fires when the node is under memory pressure and RSS is higher than TCMalloc accounts for. |
| chrony | Root | v2024.2 |
| ClockBound | Root. Optional (`is_configure_clockbound`, default false). Requires chrony. | v2025.2.3.0 |
| Node exporter | Root. Installs the bundled node exporter and restarts `node_exporter.service` on port 9300. An exporter already running under that unit is replaced with the bundled binary, then restarted. | v2024.2 |
| cgroups | User (`--noroot`). Controlled by `configure_cgroup` (default true). | v2024.2 |
| User-level systemd | User. Default (`use_system_level_systemd` false). | Cron-based universes are not supported in v2025.2 and later. |
| Node agent | User. Installed as `yugabyte` under user-level systemd. | v2024.2. Running as `yugabyte` rather than root is the v2025.2 behavior. |
| Data directories | You create the mount points before the script runs. The script checks that `yugabyte` can use them. | v2024.2 |
| Provider, instance type, and node registration | User. Runs when the [yba](#yba) fields in the script are set. | v2024.2 |

Legacy manual automation can diff against this table. The procedure for that method is [Fully manual provisioning](../software-on-prem-manual/).

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

You can [review the file](https://github.com/yugabyte/yugabyte-db/blob/{{< yb-version version="stable" format="short">}}/managed/node-agent/resources/node-agent-provision.yaml) and its defaults on GitHub. The sample values below match that file. `--config_override` addresses the same fields with dotted paths (`ynp.node_ip`, `yba.url`).

Set at least the following. Other fields in the sample have usable defaults.

- `ynp.node_ip`
- `yba.url`, `yba.customer_uuid`, `yba.api_key`
- `yba.node_name`, `yba.node_external_fqdn`
- `yba.provider.name`, `yba.provider.region.name`, `yba.provider.region.zone.name`
- `yba.instance_type.name` and `yba.instance_type.mount_points`. For an existing instance type, `mount_points` must match that type. `cores`, `memory_size`, and `volume_size` are required when the instance type is new.

Every run validates `node-agent-provision.yaml` first, including `--generate_config`, `--dry_run`, and `--preflight_check`. The sample `instance_type.cores`, `memory_size`, and `volume_size` values (`<number_of_cores>`, `<memory_in_gb>`, `<volume_in_gb>`) are not integers, so that check fails with `Failed to validate config`. Replace each with an integer, or delete the key when the instance type already exists.

`node_name` is a free-form label for the instance in YugabyteDB Anywhere. `node_external_fqdn` is the address YugabyteDB Anywhere uses to reach the node. You can set `node_name` to a hostname while `node_external_fqdn` is an IP address, or the other way around. Both the script and the manual add-instance flow accept that split.

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
| `node_exporter_port` | Not applied | The sample sets `9300`. The script always listens on 9300 and, when it creates the provider, registers 9300. A value in this key, including one `--generate_config` copies from the provider, is not applied. Set the universe port mapping to 9300. |
| `is_configure_clockbound` | Default `false` | Set to `true` to install [ClockBound](https://github.com/aws/clock-bound) and point the provider at it, so universes created from the provider set [time_source](../../../../reference/configuration/yb-master/#time-source) to `clockbound`. ClockBound requires chrony. v2025.2.3.0 and later. |
| `configure_cgroup` | Default `true` | When `true`, the script configures TServer cgroup isolation. When `false`, it skips cgroup tuning. |

#### `yba`

Set these so the script can create or update the [on-premises provider](../../../configure-yugabyte-platform/on-premises-provider/) and add the node. YugabyteDB Anywhere must be installed and running.

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

## Run the provisioning script

Run the script as root. From a root shell:

```sh
./node-agent-provision.sh
```

Where sudo is allowed:

```sh
sudo ./node-agent-provision.sh
```

Available in v2024.2 and later. With no extra flag, this is a full provision: root modules and user modules. The script provisions the node, installs node agent, and runs preflight checks.

If the `yba` fields are set, node agent creates the on-premises provider, or adds the instance when the provider already exists.

The script is idempotent. If a run stops partway through, run the same command again. The node agent directories and unit files stay in place and the re-run finishes the work. When the `yugabyte` user already exists, that user's home directory wins: the script replaces `ynp.yb_user_home` with it before the modules run. `--generate_config` writes the local user's home when the user exists, and the provider **YB Nodes Home Directory** when the user does not. Manual removal is for uninstalling YugabyteDB from a node you are taking out of service. See [Remove node agent](../../../administer-yugabyte-platform/uninstall-software/#delete-on-premises-database-server-nodes).

After the node is provisioned, reboot the node.

If the preflight check fails, rebooting the node may solve some issues (for example, incorrect ulimit settings). Then re-run the script.

<a id="split-team-provisioning"></a>

## Split root and user privileges (separation of duties)

Available in v2026.1.0.0 and later. `--root` and `--noroot` are mutually exclusive. On v2025.2 and earlier, one operator runs the full script as root.

Linux administrators often have root and no YugabyteDB context. Database administrators often have the YugabyteDB Anywhere API token and no root. The two flags split the script along that line. Run `--root` first. A later `--root` run leaves work that is already in the desired state in place (it does not create a second `yugabyte` user, and it re-applies settings that are already correct).

You can `su` to root for `--root`. A `sudo` prefix is only needed when sudo is the mechanism you use.

### New universes

1. Database administrators download the node agent package from the target YugabyteDB Anywhere version. In `node-agent-provision.yaml`, replace `yba.instance_type.cores`, `memory_size`, and `volume_size` with integers, or delete those keys. The sample placeholders fail validation before the script writes a command list. Then run a root dry run. That writes the root command list and does not change the node:

    ```sh
    sudo ./node-agent-provision.sh --root --dry_run
    ```

1. Linux administrators create the VM, install Linux and the [additional software](../#additional-software), open the [required ports](../../networking/), and either run the rendered root script or run `--root` themselves:

    ```sh
    sudo ./node-agent-provision.sh --root
    ```

    `--root` runs only modules that need elevated privileges (the `yugabyte` user, chrony, THP, ulimits, sysctl, sudoers, node exporter, and root systemd units).

1. Database administrators receive the VM, fill in `node-agent-provision.yaml`, and run `--noroot` as `yugabyte`:

    ```sh
    ./node-agent-provision.sh --noroot
    ```

    `--noroot` must run as `yugabyte`. It installs user-level systemd units and cgroups, installs node agent, and registers the node. The node then shows up in the provider free pool.

### Existing universes

Generate the YAML with `--generate_config` (see [Generate configuration files](#generate-configuration-files)), then follow [Re-provision nodes of an existing universe](#re-provision-nodes-of-an-existing-universe-non-sudo-on-premises). The Linux team runs `--root` during the maintenance window. The database team runs `--noroot`.

## Verify provisioning

After running the script and rebooting the VM, you can verify that provisioning was successful and YugabyteDB Anywhere can communicate with the node by navigating to `https://<yugabytedbanywhere-host-ip>/nodeagent`, where `yugabytedbanywhere-host-ip` is the IP address hosting your YugabyteDB Anywhere instance.

The page lists the node agents that have been activated and their status.

## Preflight check

Available in v2024.2 and later. For troubleshooting, you can run the script's preflight checks separately:

```sh
sudo ./node-agent-provision.sh --preflight_check
```

From a root shell, omit `sudo`.

Use the `--preflight_check_out_file` flag (v2025.2.4.0 and later) to specify the file path for the preflight_check output. Only the check JSON output is saved. You can read and parse the files for automation.

Provisioning also runs this check. On a node that is already in a universe, run it only while the node is in maintenance mode. The on-premises checks bind the database ports, so they cannot run while YB-Master and YB-TServer are listening. See [Re-provision nodes of an existing universe](#re-provision-nodes-of-an-existing-universe-non-sudo-on-premises).

## Dry run

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

## Override configuration

Using the `--config_override` flag (available in v2025.2.4.0 and later), you can keep one `node-agent-provision.yaml` for a fleet (YugabyteDB Anywhere URL, API token, chrony servers, home directories, provider defaults) and pass only the fields that differ per VM.

You can pass as many overrides as needed. The value after `=` is JSON, and it must match that field's type: a quoted string, a number, `true` or `false`, or a JSON array. Nested fields use dotted paths, such as `yba.instance_type.name`.

### One configuration file, many nodes

Copy the same file to every node. Override the three fields that identify the node:

```sh
./node-agent-provision.sh \
    --config_override ynp.node_ip=\"10.1.2.3\" \
    --config_override yba.node_name=\"db-node-03\" \
    --config_override yba.node_external_fqdn=\"db-node-03.example.com\"
```

`yba.node_name` is the free-form label. `yba.node_external_fqdn` is the address YugabyteDB Anywhere reaches. `ynp.node_ip` is the address other database nodes use.

### Other overrides

Override chrony servers (a list of strings):

```sh
./node-agent-provision.sh --config_override ynp.chrony_servers=[\"s1\",\"s2\"]
```

Override the FQDN and the YugabyteDB Anywhere URL together:

```sh
./node-agent-provision.sh --config_override yba.node_external_fqdn=\"my-new-fqdn\" --config_override yba.url=\"https://new-yba-url.com\"
```

## Generate configuration files

Use the `--generate_config` flag (v2025.2.4.0 and later) to write a YAML file from the node's current provider registration. The command does not provision the node. This is how you hydrate a configuration for a node that is already in the provider, before [re-provisioning an existing universe](#re-provision-nodes-of-an-existing-universe-non-sudo-on-premises).

The seed `node-agent-provision.yaml` needs `yba.url`, `yba.api_key`, and the node FQDN or IP (`yba.node_external_fqdn`). Replace `yba.instance_type.cores`, `memory_size`, and `volume_size` with integers, or delete those keys. The sample placeholders fail validation, and the script checks the seed before it writes a file. The generated file is named `node-agent-provision-generated.yaml` in the same directory. `--config_file` (default `./node-agent-provision.yaml`, available in v2024.2 and later) selects which file a later run reads. Point it at the generated file:

```sh
./node-agent-provision.sh --generate_config
./node-agent-provision.sh --config_file node-agent-provision-generated.yaml
```

When you combine [--config_override](#per-node-overrides) with `--generate_config`, overrides apply to fields used to contact YugabyteDB Anywhere (for example, `yba.url`), not to values that YugabyteDB Anywhere returns into the generated file.

Use `--generate_and_run` (v2025.2.4.0 and later) to generate the file and provision in one invocation. The script switches to the generated file for the provision.

```sh
./node-agent-provision.sh --generate_and_run
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
If the node stays in maintenance mode longer than 15 minutes, the Raft leader treats the follower as failed and re-replicates its data. The timeout is [follower_unavailable_considered_failed_sec](../../../reference/configuration/yb-tserver/#follower-unavailable-considered-failed-sec) (default 900 seconds). Raise that flag on YB-TServer and YB-Master before a slow run, and set [log_min_seconds_to_retain](../../../reference/configuration/yb-tserver/#log-min-seconds-to-retain) to the same value so the WAL is kept until the node returns. Change the flags in [Edit configuration flags](../../../manage-deployments/edit-config-flags/). See [Enter maintenance mode](../../../manage-deployments/remove-nodes/#enter-and-exit-maintenance-mode).
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

Both keys are global. A global key applies to every provider and every universe. Only a Super Admin can change one. See [Manage runtime configuration settings](../../../administer-yugabyte-platform/manage-runtime-config/).

| Key | Scope | Default | Effect |
| :--- | :--- | :--- | :--- |
| `yb.node_agent.enable_ynp_version_check` | Global | `false` | When `true`, adding a node requires the node's YNP major version to match YugabyteDB Anywhere, and health checks raise **YNP Version Skew** on a mismatch or a missing version file. There is no provider or universe override. |
| `yb.node_agent.disable_ynp_node_preflight_check` | Global | `false` | When `true`, YugabyteDB Anywhere runs the legacy `preflight_checks.sh` instead of the YNP preflight checks while it adds or validates a manual on-premises node. A preflight still runs. The checks inside `node-agent-provision.sh` are unchanged. |

## sudo whitelist

You need a sudo whitelist only when sudo is how you run the script. A root shell (`su` to root, then `./node-agent-provision.sh` with no sudo prefix) does not use the whitelist.

If security restrictions require you to explicitly list the commands that you'll be running as root under sudo, you can add the following commands to the sudo whitelist:

```sh
sudo ./node-agent-provision.sh --preflight_check
sudo ./node-agent-provision.sh
```

The underlying commands depend on the YugabyteDB Anywhere version and change as node requirements change. To see them, run a [dry run](#dry-run) and read the install script it prints. `--root --dry_run` (v2026.1.0.0 and later) is the root-only list.

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

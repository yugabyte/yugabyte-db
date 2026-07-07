---
title: Set up multitenancy
headerTitle: Set up multitenancy
linkTitle: Set up
description: Prepare cgroups and enable per-database CPU isolation in YugabyteDB.
headcontent: Prepare cgroups and enable per-database CPU isolation
menu:
  stable:
    identifier: multitenancy-set-up
    parent: multitenancy
    weight: 10
type: docs
rightNav:
  hideH4: true
---

Setting up multitenancy has two parts:

1. Prepare a writable cgroup for the YB-TServer process (a one-time, environment-specific, operating system step that typically requires root).
1. Enable the resource governor using YB-TServer and YB-Master flags.

## Prepare cgroups

When multitenancy is enabled, the YB-TServer assumes it is started in a dedicated cgroup (the _root cgroup_) that it fully manages. The YB-TServer creates and maintains a cgroup hierarchy under this root cgroup, and assigns CPU limits and threads to descendant cgroups.

You must therefore start the YB-TServer inside a cgroup that:

- has the CPU controller available;
- is writable by the user running the YB-TServer process; and
- is a leaf cgroup not managed by any other process (no other process moves processes in or out of it, or creates cgroups underneath it).

{{< warning title="One TServer per cgroup" >}}
Multiple YB-TServer processes must not run in the same cgroup.
{{< /warning >}}

Creating this cgroup and granting write access generally requires root privileges (running the YB-TServer afterwards does not). The exact steps depend on your operating system and how you run the YB-TServer.

### cgroup v2 with systemd

On most newer operating systems, there is a single unified cgroup hierarchy managed by systemd, so you configure delegation through systemd.

For a service running under a user slice (for example, a typical VM deployment), make the CPU controller available to the user slice by creating an override for the `user@.service` template (requires root). Create the file `/etc/systemd/system/user@.service.d/override.conf` with the following contents:

```conf
[Service]
Delegate=pids memory cpu
```

The YB-TServer service unit must also delegate the CPU controller:

```conf
[Service]
Delegate=cpu
```

If the YB-TServer service unit uses `ExecStartPost=`, `ExecReload=`, `ExecStop=`, or `ExecStopPost=`, it must also delegate a subgroup so that these commands don't interfere with the YB-TServer's own cgroup:

```conf
[Service]
DelegateSubgroup=ybtserver
```

`DelegateSubgroup=` requires systemd 254 or later. On earlier versions (for example, AlmaLinux 9), create the subgroup and delegate the CPU controller manually in the service before starting the YB-TServer:

```sh
SUBGROUP_NAME="$SERVICE_CGROUP/ybtserver"
# Create the subgroup.
mkdir -p "$SUBGROUP_NAME"
# Move this process into the subgroup.
echo $$ > "$SUBGROUP_NAME/cgroup.procs"
# Delegate the CPU controller (only works now that no processes are left in $SERVICE_CGROUP).
echo +cpu > "$SERVICE_CGROUP/cgroup.subtree_control"
# Start yb-tserver in the subgroup.
./yb-tserver ...
```

For a service running under the system slice, the CPU controller is available by default, so the `user@.service` override is not needed; delegate the CPU controller on the YB-TServer service unit as shown above.

### Other deployments

For deployments that don't rely on systemd (such as cron-based or container-based deployments), place the YB-TServer process in a cgroup that meets the [requirements](#prepare-cgroups). Make the CPU controller available by writing `+cpu` to the `cgroup.subtree_control` file of every ancestor cgroup, top down.

Under cgroup v2, even with write permissions to a cgroup, a non-root user can move a process into it only if the user has write access to the `cgroup.procs` file of the common ancestor of the target cgroup and the cgroup the user is currently in. Depending on the cgroup chosen, root may be required to start the YB-TServer.

Container deployments (Docker and Kubernetes) typically mount the cgroups filesystem read-only. To use multitenancy, the container must be privileged with the root cgroup hierarchy mounted read/write so that child cgroups can be created. Some platforms provide workarounds; for example, GKE supports [writable cgroups in pods](https://docs.cloud.google.com/kubernetes-engine/docs/how-to/writable-cgroups).

## Enable and configure the resource governor

The resource governor is controlled entirely through flags, set on both YB-Master and YB-TServer. To enable multitenancy, set the `enable_qos` flag to true on Masters and TServers.

Set the following flags on both YB-Masters and YB-TServers to configure multitenancy.

| Flag | Description | Default |
| :--- | :--- | :--- |
| [enable_qos](../../../reference/configuration/yb-tserver/#enable-qos) | Master switch for per-database CPU limits and the database count cap. When `false` (the default), per-database cgroups are not created and no `qos_*` flag has any effect. | `false` |
| [qos_max_db_cpu_percent](../../../reference/configuration/yb-tserver/#qos-max-db-cpu-percent) | Per-database maximum percentage of the node's non-system-reserved CPU. | `100.0` |
| [qos_max_db_count](../../../reference/configuration/yb-master/#qos-max-db-count) |  Maximum number of (non-template) databases. Sets the effective per-database minimum CPU as `1 / qos_max_db_count`. | `50` |
| [qos_evaluation_window_us](../../../reference/configuration/yb-tserver/#qos-evaluation-window-us) | Advanced. Scheduler period (µs) for checking CPU limits (`cfs_period_us`) (1000–1000000). Do not change unless necessary. | `100000` |
| [enable_reserved_cpu_for_system](../../../reference/configuration/yb-tserver/#enable-reserved-cpu-for-system) | Reserves CPU for high-priority system work by capping all other work. Can be used independently of `enable_qos`. | `false` |
| [high_priority_system_reserved_cpu](../../../reference/configuration/yb-tserver/#high-priority-system-reserved-cpu) | Amount of CPU reserved for high-priority system work (0.0–100.0). | `5.0` |

{{< note title="Note" >}}
None of the `qos_*` flags take effect unless `enable_qos` is `true`. `enable_qos` and `enable_reserved_cpu_for_system` are independent, and either can be used on its own.
{{< /note >}}

Because settings are applied as flags, you can also change the flags after the cluster is created. Note that `enable_qos` and `enable_reserved_cpu_for_system` are not runtime flags and require a restart to take effect.

### yugabyted

With yugabyted, you are responsible for setting up the root cgroup manually (see [Prepare cgroups](#prepare-cgroups)) because the required setup is highly dependent on your environment. After the cgroup is ready, pass the flags using [--tserver_flags and --master_flags](../../../reference/configuration/yugabyted/#pass-additional-flags-to-yb-master-and-yb-tserver) when starting the node.

For example, to reserve 5% of CPU for system work, cap each database at 25% of the remaining CPU, and allow at most 20 databases (a 5% per-database minimum):

```sh
./bin/yugabyted start \
    --tserver_flags="enable_qos=true,enable_reserved_cpu_for_system=true,qos_max_db_cpu_percent=25,high_priority_system_reserved_cpu=5,qos_max_db_count=20" \
    --master_flags="enable_qos=true,enable_reserved_cpu_for_system=true,qos_max_db_cpu_percent=25,high_priority_system_reserved_cpu=5,qos_max_db_count=20"
```

## Disable the resource governor

To turn off per-database CPU limits, set `enable_qos` to `false` (and, if desired, `enable_reserved_cpu_for_system` to `false`) and restart the YB-Master and YB-TServer processes. The cgroup setup performed on the operating system does not need to be undone.

## Learn more

- [Quality of service](../../../develop/quality-of-service/) for other admission control and resource management controls.

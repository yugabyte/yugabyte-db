---
title: yugabyted errors and warnings
linkTitle: Errors and warnings
description: yugabyted CLI errors and warnings
menu:
  preview:
    identifier: yugabyted-errors-warnings
    parent: yugabyted
    weight: 2455
isTocNested: true
showAsideToc: true
---

This page explains errors and warnings that you may see while running a YugabyteDB cluster using the yugabyted CLI, and ways to resolve them.

| CLI message | CLI command (task) | Severity |
| :---------- | :----------------- | :------- |
| [open files ulimits value set low](#open-files-ulimits-value-set-low) | installation | Warning |
| [max user processes value set low](#max-user-processes-value-set-low) | installation | Warning |
| [Transparent hugepages disabled](#transparent-hugepages-disabled) | installation | Warning |
| [Six loopback IPs are required](#six-loopback-ips-are-required) | installation | Warning |
| [ntp package missing](#ntp-package-missing) | installation | Warning |

## Open files ulimits value set low

YugabyteDB needs to create a large number of files for writing data, logs, and a variety of other information onto the disk. Your operating system (OS) may not be configured to allow a user process to open a large number of files.

Set the soft and hard limits to 1048576.

Refer to [Install YugabyteDB](../../../../quick-start/install/macos/) for more prerequisites.

## Max user processes value set low

YugabyteDB may create a large number of user processes to handle the user connections and app connections when running read and write workloads hence it's recommended to increase the max_user_processes of the Operating system.

To address this warning, set the soft and hard limits to at least 12000 (for Linux) and 2500 (for macOS).

Refer to [Install YugabyteDB](../../../../quick-start/install/macos/) for more prerequisites.

## Transparent hugepages disabled

Transparent Huge Pages (THP) is a Linux memory management system that uses larger memory pages to reduce the overhead of Translation Lookaside Buffer (TLB) lookups on machines with large amounts of memory.

Enable `transparent_hugepages` on your computer or VM to resolve this issue.

See [transparent hugepages](../../../../deploy/manual-deployment/system-config/#transparent-hugepages) to learn more about this system configuration.

## Six loopback IPs are required

When you're running a cluster (for testing or development) on a single computer, you assign loopback addresses to the master and tablet servers. For example, a replication factor of 3 (RF=3) requires six loopback IP addresses.

To resolve this issue, run `sudo ifconfig lo0 alias 127.0.0.x` six times with different IP address values, such as 127.0.0.2 through 127.0.0.7.

Refer to [Install YugabyteDB](https://docs.yugabyte.com/latest/quick-start/install/macos/) for more prerequisites.

## NTP package missing

For YugabyteDB to preserve data consistency, the clock drift and clock skew across different nodes must be bounded. This can be achieved by running clock synchronization software, such as NTP or chrony. Below are some recommendations on how to configure clock synchronization.

To address this warning, make sure the ntp package is installed on YugabyteDB nodes.

Refer to the [deployment checklist](../../../../deploy/checklist/#clock-synchronization) for more clock synchronization requirements.



---
title: yugabyted Errors and Warnings
linkTitle: Errors and Warnings
description: yugabyted CLI errors and warnings
section: REFERENCE
menu:
  latest:
    identifier: yugabyted-errors-warnings
    parent: configuration
    weight: 2455
isTocNested: true
showAsideToc: true
---

This page explains errors and warnings that you may experience while running a YugabyteDB cluster using yugabyted CLI and ways to resolve them.

| CLI Message | Command/Task | Error/Warning |
| --- | --- | --- |
| [open files ulimits value set low](#open-files-ulimits-value-set-low) | installation | Warning |
| --- | --- | --- |
| [max user processes value set low](#max-user-processes-value-set-low) | installation | Warning |
| [Transparent hugepages disabled](#transparent-hugepages-disabled) | installation | Warning |
| [Six loopback IPs are required](#six-loopback-ips-are-required) | installation | Warning |
| [ntp package missing](#ntp-package-missing) | installation | Warning |

## open files ulimits value set low

YugabyteDB needs to create a large number of files which may not be enabled/allowed by the OS by default.

We recommend setting the soft and hard limits to 1048576.

To learn more about prerequisites for installing YugabyteDB, see [Install YugabyteDB](https://docs.yugabyte.com/latest/quick-start/install/macos/).

## max user processes value set low

YugabyteDB needs to create many different users while running benchmarks which requires high max\_user\_processes.

To address this warning, we recommend setting the soft and hard limits to 12000(For Linux) and 2500(For MacOS).

To learn more about prerequisites for installing YugabyteDB, see [Install YugabyteDB](https://docs.yugabyte.com/latest/quick-start/install/macos/).

## Transparent hugepages disabled

Transparent Huge Pages (THP) is a Linux memory management system that reduces the overhead of Translation Lookaside Buffer (TLB) lookups on machines with large amounts of memory by using larger memory pages.

Enable &#39;transparent\_hugepages&#39; on your computer or VM to resolve this issue.

See [transparent hugepages](https://docs.yugabyte.com/latest/deploy/manual-deployment/system-config/#transparent-hugepages) to learn more about this system configuration.

## Six loopback IPs are required

loopback addresses are assigned to master and tservers that would run on the local machine. An RF=3 would require six loopback IPs.

To resolve this issue, run &quot;sudo ifconfig lo0 alias 127.0.0.x&quot; six times with different IP values replacing &#39;x&#39; in the octet, e.g. 127.0.0.2, 127.0.0.7, etc.

To learn more about prerequisites for installing YugabyteDB, see [Install YugabyteDB](https://docs.yugabyte.com/latest/quick-start/install/macos/).

## ntp package missing

For YugabyteDB to preserve data consistency, the clock drift and clock skew across different nodes must be bounded. This can be achieved by running clock synchronization software, such as NTP or chrony. Below are some recommendations on how to configure clock synchronization.

To address this warning, make sure the ntp package is installed on YugabyteDB nodes.

See [deployment checklist](https://docs.yugabyte.com/latest/deploy/checklist/#clock-synchronization) to learn more about clock synchronization requirements for YugabyteDB.



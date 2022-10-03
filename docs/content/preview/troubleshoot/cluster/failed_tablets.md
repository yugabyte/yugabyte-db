---
title: Recover YB-TServer from crash loop
linkTitle: Recover YB-TServer from crash loop
description: Recover YB-TServer from crash loop
menu:
  preview:
    parent: troubleshoot-cluster
    weight: 866
type: docs
---

When a YB-TServer process or node has failed, YugabyteDB automatically triggers a remote bootstrap for most types of tablet data corruption or failures. However, in some cases the automatic bootstrap may not solve the problem, resulting in a crash loop. This can happen when a condition leads to a code path encoded as a crash (for example, `CHECK` or `FATAL`) or when the cause of a crash is unknown (for example, code errors that lead to `SIGSEGV`). In all of these cases, the root cause would likely repeat itself when the process is restarted. Moreover, when using YugabyteDB Anywhere, crashed processes are automatically restarted to ensure minimum downtime.

Since servers stuck in a crash loop typically cannot stay up long enough to be safely issued runtime commands against them, manual intervention by the administrator is required to bring a YB-TServer back to a healthy state.

To do this, the administrator needs to find all the faulty tablets, look for their data on disk (possibly spread across multiple disks, depending on your `fs_data_dirs`), and then remove it.

The following are the steps to address this scenario:

1. Stop the YB-TServer process to prevent new restarts during operations. For YugabyteDB Anywhere, execute the `yb-server-ctl tserver stop` command.
2. Find the tablets that are encountering these problems. You may [consult logs](../../nodes/check-logs) to get the UUID of tablets. In the described scenario, the UUID of the tablet is `FOO` and the `--fs_data_dirs` flag is `/mnt/disk1`.
3. Find and remove all the tablet files, as follows:

   ```bash
   find /mnt/disk1 -name '*FOO*' | xargs rm -rf
   ```

4. Repeat the preceding command for each disk in `--fs_data_dirs`.
5. Restart the YB-TServer process. In YugabyteDB Anywhere, execute the `yb-server-ctl tserver start` command.

When completed, the YB-TServer should be able to start, stay alive, and rejoin the cluster, while the centralized load balancer re-replicates or redistributes copies of any affected tablets.

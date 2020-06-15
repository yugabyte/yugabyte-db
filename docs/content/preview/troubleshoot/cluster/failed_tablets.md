---
title: Recover YB-TServer from crash loop
linkTitle: Recover YB-TServer from crash loop
description: Recover YB-TServer from crash loop
menu:
  preview:
    parent: troubleshoot-cluster
    weight: 866
isTocNested: true
showAsideToc: true
---

When a YB-TServer process or node has failed, YugabyteDB will automatically trigger a remote bootstrap for
 most types of tablet data corruption or failures. However, there are still a number of cases where this automatic mechanism might not be able to help.

One major such case is anything that leads to a code path that we've encoded as a crash (eg: `CHECK` or `FATAL`) or is an unknown unknown 
that causes a crash (eg: code bugs leading to `SIGSEGV`). In all of these cases, it's likely the root cause would repeat itself, when the process is brought back up! 
Moreover, when using YugabyteDB Anywhere, crashed processes will be automatically restarted, to ensure minimum downtime! 

The problem with servers stuck in such a crash loop though, is that they would likely not be up long enough for us to be able to safely issue runtime commands against them. 
At that point, manual admin intervention is required, to bring a yb-tserver back to a healthy state!

For this, we'll find all the bad tablets, look for their data on disk (note: this may be spread across multiple disks, depending on your `fs_data_dirs`), and then remove it.
Here are the steps to address this scenario:

1. Stop the yb-tserver process (to prevent new restarts, during operations). For YugabyteDB Anywhere, run `yb-server-ctl tserver stop`.
2. Find the tablet(s) that are hitting these problems. We can [look into logs](../../nodes/check-logs) to get the UUID of tablet(s).  In this scenario, the UUID of the tablet is `FOO` and the `--fs_data_dirs` flag for this is `/mnt/disk1`.

3. Find and remove all the tablet files: 
```bash
find /mnt/disk1 -name '*FOO*' | xargs rm -rf
```
4. You need to repeat the command above for each disk in `--fs_data_dirs`
5. Start back the yb-tserver process. In YugabyteDB Anywhere, run `yb-server-ctl tserver start`

Now the tserver should be able to start, stay alive and re-join the cluster, while the centralized load balancer will re-replicate or re-distribute copies of any affected tablets.

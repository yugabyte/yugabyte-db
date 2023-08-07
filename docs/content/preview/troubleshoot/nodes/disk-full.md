---
title: Disk Full
linkTitle: Disk Full
headerTitle: Disk Full issue
description: Learn how to address disk full issue
menu:
  preview:
    parent: troubleshoot-nodes
    weight: 40
type: docs
---

## Disk space full on crash loop

During a YB-TServer crash loop, the disk can fill up, preventing recovery.

During the tablet bootstrap that takes place during a YB-TServer restart, a new WAL file is allocated, irrespective of the amount of logs generated in the server's previous WAL file. If a YB-TServer has several tablets and goes into a crash loop, each one of the tablets creates new WAL files on every bootstrap, filling up the disk.

Starting with v2.18.1, you can prevent repeated WAL file allocation in a crash loop by using the [--reuse_unclosed_segment_threshold_bytes](../../../reference/configuration/yb-tserver/#reuse-unclosed-segment-threshold-bytes) flag.

If a tablet's last WAL file size is less than or equal to this threshold value, the bootstrap process will reuse the last WAL file rather than create a new one. To set the flag so that the last WAL file is always reused, you can set the flag to the current maximum WAL file size (64MB), as follows:

```sh
reuse_unclosed_segment_threshold_bytes=67108864
```

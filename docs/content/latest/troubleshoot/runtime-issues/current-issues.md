---
title: Current known issues
linkTitle: Current known issues
description: Current known issues
aliases:
  - /troubleshoot/issues-current
menu:
  latest:
    identifier: troubleshoot-issues-current
    parent: troubleshoot-issues
    weight: 772
isTocNested: true
showAsideToc: true
---

## Stuck inbound ports / rpcs #2562
### Status
Present in 1.2, 1.3, 2.x.
WIP D7521.

### Description
If a TS hits the memory limit, our network stack will stop reading from sockets.

### Frequent causes
Memory growth hits the hard limit.

### Detection
Logged in as centos, check the network queues using:
sudo netstat -anp | grep 9100

Example bad output

tcp   4346793      0 10.232.124.161:9100     10.232.150.133:50844    ESTABLISHED

If the inbound queue is full (second column above), the TS is most likely stuck.

### Mitigation
Restart the TS.

---
title: Fixed issues
linkTitle: Fixed issues
description: Fixed issues
aliases:
  - /troubleshoot/issues-fixed
menu:
  latest:
    identifier: troubleshoot-issues-fixed
    parent: troubleshoot-issues
    weight: 732
isTocNested: true
showAsideToc: true
---

## LB stuck on pending delete #424

### Status
Present in 1.2.
Fixed in 1.3.2.1 by D6809.

### Description
If a master DeleteTablet task times out (default 3600s) without successfully deleting a tablet on a TS, the master in memory state does not reset the bit for pending deletes. 

### Frequent causes
Network partitions or the stuck network bug (#2562).

### Detection
Check master leader logs for tablet server <UUID> has a pending delete

Do note that it is possible this message shows up transiently. However, if it has been more than 3600s (default max task timeout) since it has been logged, the memory state is definitely stuck.

### Mitigation
Restart the master leader.

## TBD Tablets stuck in FAILED state on a TS
TBD

### Status
TBD

### Description
TBD

### Frequent causes
TBD

### Detection
TBD

### Mitigation
TBD

## TBD Untracked memory growth can lead to OOM

### Status
### Description
### Frequent causes
### Detection
### Mitigation

## TBD Master tables deleted but tablets stuck in RUNNING

### Status
### Description
### Frequent causes
### Detection
### Mitigation

## TBD Master tables deleted but index not

### Status
### Description
### Frequent causes
### Detection
### Mitigation

## TBD YSQL drop database breaks YCQL system queries

### Status
### Description
### Frequent causes
### Detection
### Mitigation

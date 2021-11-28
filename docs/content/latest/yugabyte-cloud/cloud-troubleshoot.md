---
title: Troubleshoot Yugabyte Cloud
linkTitle: Troubleshoot
description: Troubleshoot issues in Yugabyte Cloud.
headcontent:
image: /images/section_icons/index/quick_start.png
menu:
  latest:
    identifier: cloud-troubleshoot
    parent: yugabyte-cloud
    weight: 850
isTocNested: true
showAsideToc: true
---

## Connecting to clusters

### Connection timed out

```output
ysqlsh: could not connect to server: Operation timed out
    Is the server running on host "4477b40e-8f1c-4ee0-8f95-f32e5abf5f76.aws.ybdb.io" (44.144.244.144) and accepting
    TCP/IP connections on port 5433?
```

If you are trying to connect to a cluster from your local computer, add your computer to the cluster IP allow list.

If you have a VPC configured, add one or more IP addresses from the peered VPN to the cluster IP allow list.

### SSL off

```output
ysqlsh: FATAL:  no pg_hba.conf entry for host "144.244.44.44", user "admin", database "yugabyte", SSL off
```

Yugabyte Cloud clusters require an SSL connection. If you set `sslmode` to `disable`, your connection will fail.

### Application fails to connect

Be sure to encode any special characters in your connection string.

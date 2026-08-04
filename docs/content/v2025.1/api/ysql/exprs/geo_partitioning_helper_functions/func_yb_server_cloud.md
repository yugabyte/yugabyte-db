---
title: yb_server_cloud() function [YSQL]
headerTitle: yb_server_cloud()
linkTitle: yb_server_cloud()
description: Returns the cloud provider of the currently connected node
menu:
  v2025.1_api:
    identifier: api-ysql-exprs-yb_server_cloud
    parent: geo-partitioning-helper-functions
type: docs
---

## Synopsis

`yb_server_cloud()` returns the cloud provider that a user's server is connected to.

## Examples

Call `yb_server_cloud()`

```plpgsql
yugabyte=# SELECT yb_server_cloud();
```

```output.sql
 yb_server_cloud
-----------------
 aws
(1 row)
```

## See also

- [`yb_server_region()`](../func_yb_server_region)
- [`yb_server_zone()`](../func_yb_server_zone)
- [`yb_is_local_table(oid)`](../func_yb_is_local_table)

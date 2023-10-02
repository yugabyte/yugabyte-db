---
title: yb_server_zone() function [YSQL]
headerTitle: yb_server_zone()
linkTitle: yb_server_zone()
description: Returns the zone of the currently connected node
menu:
  v2.16:
    identifier: api-ysql-exprs-yb_server_zone
    parent: geo-partitioning-helper-functions
type: docs
---

## Synopsis

`yb_server_zone()` returns the zone that a user's server is connected to.

## Examples

Select the zone that you're connected to:

```plpgsql
yugabyte=# SELECT yb_server_zone();
```

```output.sql
 yb_server_zone
-----------------
 us-west-1c
(1 row)
```

## Usage in Row-level geo-partitioning

Similar to [`yb_server_region()`](../func_yb_server_region), this function is also helpful while implementing [Row-level geo-partitioning](../../../../../explore/multi-region-deployments/row-level-geo-partitioning/), as it can significantly simplify inserting rows from the user server's partition if the partitioning is based on default value of yb_server_zone().

{{< note title="Note" >}}

If you didn't set the placement_region flag at node startup, yb_server_region() returns NULL.

{{< /note >}}

## See also

- [`yb_server_cloud()`](../func_yb_server_cloud)
- [`yb_server_region()`](../func_yb_server_region)
- [`yb_is_local_table(oid)`](../func_yb_is_local_table)

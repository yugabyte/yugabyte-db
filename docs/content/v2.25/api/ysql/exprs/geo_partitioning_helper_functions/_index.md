---
title: Geo-partitioning helper functions
headerTitle: Geo-partitioning helper functions
linkTitle: Geo-partitioning helper functions
description: This section contains all the helper functions for geo-distribution.
image: /images/section_icons/api/subsection.png
menu:
  v2.25_api:
    identifier: geo-partitioning-helper-functions
    parent: api-ysql-exprs
    weight: 40
type: indexpage
---

## Synopsis

The following functions are helpful for [Row-level geo-partitioning](../../../../explore/multi-region-deployments/row-level-geo-partitioning/), as they make it easier to insert rows from a user's server and select rows from the local partition.

| Function | Return Type |Description |
|-----------|------------|-------------|
| [yb_is_local_table(oid)](func_yb_is_local_table) | boolean | Returns whether the given 'oid' is a table replicated only in the local region |
| [yb_server_region()](func_yb_server_region) | varchar | Returns the region of the currently connected node |
| [yb_server_zone()](func_yb_server_zone) | varchar | Returns the zone of the currently connected node |
| [yb_server_cloud()](func_yb_server_cloud) | varchar | Returns the cloud provider of the currently connected node |

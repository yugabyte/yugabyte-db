---
title: Network security
headerTitle: Network security
linkTitle: Network security
description: Network security
menu:
  v2.18_yugabyte-platform:
    parent: security
    identifier: network-security
    weight: 30
type: docs
---

You need to ensure that YugabyteDB Anywhere and the database run in a trusted network environment. You should restrict machine and port access, based on the following guidelines:

- Servers running YugabyteDB services are directly accessible only by YugabyteDB Anywhere, servers running the application, and database administrators.
- Only YugabyteDB Anywhere and servers running applications can connect to YugabyteDB services on the RPC ports. Access to the YugabyteDB ports should be denied to everybody else.

For information on ports that need to be opened on the YugabyteDB servers for YugabyteDB Anywhere and other applications to connect, see [Default ports](../../../reference/configuration/default-ports).

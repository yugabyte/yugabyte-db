---
title: Host-based authentication
linkTitle: Host-based authentication
description: Host-based authentication using ysql_hba_conf
headcontent: Enabling host-based authentication in YugabyteDB.
image: /images/section_icons/secure/authentication.png
menu:
  latest:
    identifier: host-based-authentication
    parent: secure
    weight: 750
---

Start yb-tserver with 

```
--ysql_hba_conf="host all yugabyte 0.0.0.0/0 trust,host all all 0.0.0.0/0 md5,host all yugabyte ::0/0 trust,host all all ::0/0 md5"
```

This will enable authentication for all users except yugabyte
---
title: Host-based authentication
linkTitle: Host-based authentication
description: Host-based authentication
headcontent: Enable encryption at rest with a user generated key
image: /images/section_icons/secure/authentication.png
menu:
  latest:
    identifier: ycql-authentication
    parent: authentication
    weight: 20
isTocNested: true
showAsideToc: true
---

Start yb-tserver with 

```
--ysql_hba_conf="host all yugabyte 0.0.0.0/0 trust,host all all 0.0.0.0/0 md5,host all yugabyte ::0/0 trust,host all all ::0/0 md5"
```

This will enable authentication for all users except yugabyte
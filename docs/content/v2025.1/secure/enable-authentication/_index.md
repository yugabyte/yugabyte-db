---
title: Enable authentication
headerTitle: Enable authentication
linkTitle: Enable authentication
description: Enable authentication.
headcontent: Enable authentication to have clients provide valid credentials before they can connect
menu:
  v2025.1:
    name: Enable authentication
    identifier: enable-authentication
    parent: secure
    weight: 715
type: indexpage
---

Enabling user authentication in YSQL and YCQL requires setting the appropriate flags on server startup:

- `--ysql_enable_auth=true` in YSQL
- `--use_cassandra_authentication=true` in YCQL

In YSQL, further fine-grained control over client authentication is provided by setting the `--ysql_hba_conf_csv` flag. You can define rules for access to localhost and remote clients based on IP addresses, authentication methods, and use of TLS (aka SSL) certificates.

{{<index/block>}}

  {{<index/item
    title="Enable user authentication"
    body="Enable authentication and configure user authorization in YugabyteDB."
    href="authentication-ysql/"
    icon="fa-thin fa-user-lock">}}

  {{<index/item
    title="Create login profiles"
    body="Prevent brute force exploits by enabling login profiles in YSQL."
    href="ysql-login-profiles/"
    icon="fa-thin fa-head-side">}}

  {{<index/item
    title="Configure client authentication"
    body="Use the ysql_hba_conf_csv flag to configure client authentication in YSQL."
    href="ysql_hba_conf-configuration/"
    icon="fa-thin fa-user-gear">}}

{{</index/block>}}

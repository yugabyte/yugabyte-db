---
title: Enable authentication
headerTitle: Enable authentication
linkTitle: Enable authentication
description: Enable authentication.
headcontent: Enable authentication to have clients provide valid credentials before they can connect to a YugabyteDB cluster.
image: /images/section_icons/secure/authentication.png
menu:
  v2.14:
    name: Enable authentication
    identifier: enable-authentication
    parent: secure
    weight: 715
type: indexpage
---

Enabling user authentication in YSQL and YCQL requires setting the appropriate flags on server startup:

- `--ysql_enable_auth=true` in YSQL
- `--use_cassandra_authentication=true` in YCQL

In YSQL, further fine-grained control over client authentication is provided by enabling the `--ysql_enable_profile` flag and setting the `--ysql_hba_conf_csv` flag. You can define rules for access to localhost and remote clients based on IP addresses, authentication methods, and use of TLS (aka SSL) certificates.

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="ysql/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/authentication.png" aria-hidden="true" />
        <div class="title">Enable user authentication</div>
      </div>
      <div class="body">
          Enable authentication and configure user authorization in YugabyteDB.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="ysql-login-profiles/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/authentication.png" aria-hidden="true" />
        <div class="title">Create login profiles</div>
      </div>
      <div class="body">
          Prevent brute force exploits by enabling login profiles in YSQL.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="ysql_hba_conf-configuration/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/authentication.png" aria-hidden="true" />
        <div class="title">Configure client authentication</div>
      </div>
      <div class="body">
          Use the ysql_hba_conf_csv flag to configure client authentication in YSQL.
      </div>
    </a>
  </div>

</div>

---
title: Authentication
linkTitle: Authentication
description: Authentication
headcontent: Instructions for enabling authentication in YugabyteDB.
image: /images/section_icons/secure/authentication.png
aliases:
  - /secure/authentication/
menu:
  latest:
    identifier: authentication
    parent: secure
    weight: 710
---

Authentication should be enabled to verify the identity of a client that connects to YugabyteDB. Note the following:

- Authentication is implemented for YSQL (PostgreSQL-compatible), YCQL (Cassandra-compatible) and YEDIS (Redis-compatible) APIs currently.

- For YSQL and YCQL, enabling authentication automatically enables authorization or role based access control (RBAC) to determine the access privileges. Authentication verifies the identity of a user while authorization determines the verified user’s database access privileges.

<div class="row">
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="ysql-authentication/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/authentication.png" aria-hidden="true" />
        <div class="title">YSQL authentication</div>
      </div>
      <div class="body">
          Enable YSQL authentication to identify that a YSQL user is who they say they are.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="ycql-authentication/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/authentication.png" aria-hidden="true" />
        <div class="title">Host-based authentication using yb_hba.conf</div>
      </div>
      <div class="body">
          Configure yb_hba.conf to control access to remote clients with host-based authentication.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="ycql-authentication/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/authentication.png" aria-hidden="true" />
        <div class="title">YCQL authentication</div>
      </div>
      <div class="body">
          Enable YSQL authentication to identify that a YCQL user is who they say they are.
      </div>
    </a>
  </div>
</div>

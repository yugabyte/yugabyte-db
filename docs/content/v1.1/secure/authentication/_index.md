---
title: Authentication
linkTitle: Authentication
description: Authentication
headcontent: Instructions for enabling authentication in YugaByte DB.
image: /images/section_icons/secure/authentication.png
aliases:
  - /secure/authentication/
menu:
  v1.1:
    identifier: secure-authentication
    parent: secure
    weight: 710
---

Authentication should be enabled to verify the identity of a client that connects to YugaByte DB. Note the following:

- Authentication is implemented for YCQL (Cassandra-compatible) and YEDIS (Redis-compatible) APIs currently.

- For YCQL, enabling authentication automatically enables authorization or role based access control (RBAC) to determine the access privileges. Authentication verifies the identity of a user while authorization determines the verified user’s access privileges to the database.

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#ycql" class="nav-link active" id="ycql-tab" data-toggle="tab" role="tab" aria-controls="ycql" aria-selected="true">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
  <li>
    <a href="#yedis" class="nav-link" id="ycql-tab" data-toggle="tab" role="tab" aria-controls="ycql" aria-selected="false">
      <i class="icon-redis" aria-hidden="true"></i>
      YEDIS
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="ycql" class="tab-pane fade show active" role="tabpanel" aria-labelledby="ycql-tab">
    {{% includeMarkdown "cassandra.md" /%}}
  </div>
  <div id="yedis" class="tab-pane fade" role="tabpanel" aria-labelledby="yedis-tab">
    {{% includeMarkdown "redis.md" /%}}
  </div>
</div>


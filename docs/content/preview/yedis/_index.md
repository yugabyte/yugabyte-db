---
title: YEDIS
linkTitle: YEDIS
headerTitle: Yugabyte Dictionary Service (YEDIS)
description: (Deprecated) The YEDIS API allows YugabyteDB to function as a clustered, auto-sharded, globally distributed and persistent key-value database that is compatible with the Redis commands library.
headcontent:
image: /images/section_icons/api/yedis.png
menu:
  preview:
    identifier: yedis
    parent: misc
    weight: 2900
type: indexpage
cascade:
  unversioned: true
---

{{< warning title="Deprecated" >}}
Yugabyte's current focus is on the two distributed SQL APIs, namely [YSQL](../api/ysql/) (PostgreSQL-compatible fully-relational API) and [YCQL](../api/ycql/) (a semi-relational API with Cassandra QL roots). **Given that YEDIS is not a focus, it must be viewed as a deprecated API for new application development purposes.**
{{< /warning >}}

The YEDIS API allows YugabyteDB to function as a persistent, resilient, auto-sharded, globally-distributed key-value database that is compatible with the Redis command library. A Redis client can connect, send requests, and receive results from this API.

{{< note title="Redis compatibility" >}}
While YEDIS supports many Redis data types (such as string, hash, set, sorted set, and a new time series type) and commands, there are some notable exceptions:

* Only a subset of sorted set commands (ZCARD, ZADD, ZRANGEBYSCORE, ZREM, ZRANGE, ZREVRANGE, ZSCORE) have been implemented. Several commands like ZCOUNT, ZREVRANGEBYSCORE, ZRANK, ZREVRANK are not implemented.

* List, Bitmaps, HyperLogLogs, and GeoSpatial types/commands are not implemented.

* YugabyteDB is a full-featured disk-based database and does not have an in-memory only architecture like Redis.

YEDIS is not a drop-in replacement to an existing Redis app. For key-value workloads that need persistence, elasticity and fault tolerance, [YCQL](../api/ycql/) (with features like keyspaces, tables, role-based access control, and more) is often a great fit, especially if the application is new rather than an existing one already written in Redis. The [YCQL drivers](../drivers-orms/) are also more clustering aware in terms of routing the request directly to the node which hosts the row/key, and hence YCQL even performs marginally better than YEDIS for equivalent scenarios. In general, our new feature development (support for data types, built-ins, TLS, backups, and more), correctness testing (using Jepsen, for example) and performance work is in the YSQL and YCQL areas.
{{< /note >}}

<div class="row">
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="quick-start/">
      <div class="head">
        <img class="icon" src="/images/section_icons/index/quick_start.png" aria-hidden="true" />
        <div class="title">Quick Start</div>
      </div>
      <div class="body">
          Get started.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="develop/">
      <div class="head">
        <img class="icon" src="/images/section_icons/index/develop.png" aria-hidden="true" />
        <div class="title">Develop</div>
      </div>
      <div class="body">
          Develop apps.
      </div>
    </a>
  </div>
  <!--
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="deploy/">
      <div class="head">
        <img class="icon" src="/images/section_icons/index/deploy.png" aria-hidden="true" />
        <div class="title">Deploy</div>
      </div>
      <div class="body">
         Deploy on the infrastructure of your choice.
      </div>
    </a>
  </div>
  -->
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="api/">
      <div class="head">
        <img class="icon" src="/images/section_icons/index/api.png" aria-hidden="true" />
        <div class="title">API reference</div>
      </div>
      <div class="body">
         Complete API reference.
      </div>
    </a>
  </div>
</div>

---
date: 2016-03-09T20:08:11+01:00
title: Develop Apps
weight: 54
---

The YugaByte DB package ships with multiple Apache Cassandra Query Language (CQL) and Redis sample apps that you can use as a starting point for developing your own application. The CQL apps available are `CassandraHelloWorld`, `CassandraKeyValue`, `CassandraStockTicker`, `CassandraTimeseries`, `CassandraUserId`, `CassandraSparkWordCount`. The Redis apps available are `RedisKeyValue` and `RedisPipelinedKeyValue`.

You can get help on all these apps by simply running the command below.

```sh
$ java -jar ./java/yb-sample-apps.jar --help
```

The source code for these apps is also available in the same directory as the compiled jar. Run the command below to expand the source jar files into `com/yugabyte/sample/apps` path of the current directory.

```sh
$ jar xf ./java/yb-sample-apps-sources.jar
```

<div>
  <a class="section-link icon-offset" href="cql/">
    <div class="icon">
      <img src="/images/section_icons/develop/cql.png" aria-hidden="true" />
    </div>
    <div class="text">
      Develop CQL apps
    </div>
  </a>

  <a class="section-link icon-offset" href="redis/">
    <div class="icon">
      <img src="/images/section_icons/develop/redis.png" aria-hidden="true" />
    </div>
    <div class="text">
      Develop Redis apps
    </div>
  </a>
</div>

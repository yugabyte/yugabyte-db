---
title: Sample datasets
linkTitle: Sample datasets
description: Sample datasets
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
section: REFERENCE
menu:
  stable:
   identifier: sample-data
   weight: 2950
---

YugabyteDB and YugabyteDB client shell installations include sample datasets you can use to test out YugabyteDB. These are located in the `share` directory of your installation. The datasets are also available in the [sample directory of the YugabyteDB GitHub repository](https://github.com/yugabyte/yugabyte-db/tree/master/sample). The datasets are provided in the form of SQL script files. You can install and use the sample datasets using either a local installation of YugabyteDB, or by connecting to a cluster in Yugabyte Cloud (including your free cluster) via a client shell.

All of the datasets are PostgreSQL-compatible, and you can explore them using the [ysqlsh shell](../admin/ysqlsh/) to compare between PostgreSQL and the YugabyteDB [YSQL API](../api/ysql/).

For information on installing YugabyteDB, refer to [Quick Start](../quick-start/).

For information on connecting to your Yugabyte Cloud cluster using `ysqlsh`, refer to [Connect via Client Shell](../yugabyte-cloud/cloud-basics/connect-to-clusters#connect-via-client-shell). To get started with Yugabyte Cloud, refer to [Get Started](../yugabyte-cloud/cloud-basics/). The exercises can be run on free or paid clusters. Cloud shell does not support loading the data, but once the data is loaded, you can use cloud shell to run exercises and query the database.

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
      <a class="section-link icon-offset" href="chinook/">
          <div class="head">
              <img class="icon" src="/images/section_icons/sample-data/s_s2-chinook-3x.png" aria-hidden="true" />
              <div class="title">Chinook</div>
          </div>
          <div class="body">
              Explore the popular sample dataset for a digital media store.
          </div>
      </a>
  </div>
  
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
      <a class="section-link icon-offset" href="northwind/">
          <div class="head">
              <img class="icon" src="/images/section_icons/sample-data/s_s3-northwind-3x.png" aria-hidden="true" />
              <div class="title">Northwind</div>
          </div>
          <div class="body">
              Explore the classic sales datasets for Northwind Traders.
          </div>
      </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
      <a class="section-link icon-offset" href="pgexercises/">
          <div class="head">
              <img class="icon" src="/images/section_icons/sample-data/s_s4-pgexercises-3x.png" aria-hidden="true" />
              <div class="title">PgExercises</div>
          </div>
          <div class="body">
              Learn SQL and test your knowledge by creating the PostgreSQL Exercises database and trying out the available exercises.
          </div>
      </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
      <a class="section-link icon-offset" href="sportsdb/">
          <div class="head">
              <img class="icon" src="/images/section_icons/sample-data/s_s5-sportsdb-3x.png" aria-hidden="true" />
              <div class="title">SportsDB</div>
          </div>
          <div class="body">
              Explore sample sports statistics for baseball, football, basketball, ice hockey, and soccer.
          </div>
      </a>
  </div>

   <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="retail-analytics/">
      <div class="head">
        <img class="icon" src="/images/section_icons/develop/apps/e-commerce.png" aria-hidden="true" />
        <div class="title">Retail Analytics</div>
      </div>
      <div class="body">
          Ad-hoc analytics of retail sales data using YugabyteDB's YSQL API.
      </div>
    </a>
  </div>

</div>

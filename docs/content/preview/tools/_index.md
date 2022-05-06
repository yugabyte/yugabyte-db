---
title: Third party tools
headerTitle: Third party tools
linkTitle: Third party tools
description: Interactive third party clients that you can use with YugabyteDB.
headcontent: Interactive third party clients that you can use with YugabyteDB to run queries and get results from.
image: /images/section_icons/troubleshoot/troubleshoot.png
aliases:
  - /develop/tools/
  - /preview/develop/tools/
section: REFERENCE
menu:
  preview:
    identifier: tools
    weight: 2900
---

Because YugabyteDB is PostgreSQL-compatible, you can use third-party PostgreSQL clients to connect to your YugabyteDB clusters.

### YugabyteDB prerequisites

Your YugabyteDB cluster should be up and running.

To create a local cluster, follow the steps in [Quick start](../../quick-start/install).

For YugabyteDB Managed, do the following:

- [Create a cluster](../../yugabyte-cloud/cloud-quickstart/)
- [Download the cluster certificate](../../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#download-your-cluster-certificate)
- [Add your computer to the IP allow list](../../yugabyte-cloud/cloud-secure-clusters/add-connections/#assign-an-ip-allow-list-to-a-cluster)

#### Connection parameters

To connect, follow the client's configuration steps for PostgreSQL, and use the following values:

| Setting | Local installation | YugabyteDB Managed |
| :--- | :--- | :--- |
| Hostname | `localhost` | The cluster hostname as displayed on the cluster **Settings** tab |
| Port | `5433` (YSQL) `9042` (YCQL) | `5433` (YSQL) `9042` (YCQL) |
| Database | The database name (`yugabyte` is the default) | The database name (`yugabyte` is the default) |
| Username | `yugabyte` | Database username; the default user is `admin` |
| Password | `yugabyte`; leave blank if YSQL authentication is not enabled | Database user password |

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="superset/">
      <div class="head">
        <img class="icon" src="/images/develop/tools/superset/superset-icon.png" aria-hidden="true" />
        <div class="title">Apache Superset</div>
      </div>
      <div class="body">
        Open-source data exploration and visualization tool.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="arctype/">
      <div class="head">
        <img class="icon" src="/images/develop/tools/arctype/arctype-icon.png" aria-hidden="true" />
        <div class="title">Arctype</div>
      </div>
      <div class="body">
        SQL client and database management tool.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="dbeaver-ysql/">
      <div class="head">
        <img class="icon" src="/images/develop/tools/dbeaver-icon.png" aria-hidden="true" />
        <div class="title">DBeaver</div>
      </div>
      <div class="body">
        Eclipse-based, multi-platform database tool.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="dbschema/">
      <div class="head">
        <img class="icon" src="/images/develop/tools/dbschema/dbschema-icon.png" aria-hidden="true" />
        <div class="title">DbSchema</div>
      </div>
      <div class="body">
        Visual database tool for reverse-engineering schemas, editing ER diagrams, browsing data, and more.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="pgadmin/">
      <div class="head">
        <img class="icon" src="/images/develop/tools/pgadmin-icon.png" aria-hidden="true" />
        <div class="title">pgAdmin</div>
      </div>
      <div class="body">
        Management tool for PostgreSQL.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="sql-workbench/">
      <div class="head">
        <img class="icon" src="/images/develop/tools/sql-workbench.png" aria-hidden="true" />
        <div class="title">SQL Workbench/J</div>
      </div>
      <div class="body">
        Unified developer console for querying YugabyteDB and displaying results.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="tableplus/">
      <div class="head">
        <img class="icon" src="/images/section_icons/develop/tools/tableplus.png" aria-hidden="true" />
        <div class="title">TablePlus</div>
      </div>
      <div class="body">
        Unified developer console for querying YugabyteDB APIs.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="visualstudioworkbench">
      <div class="head">
        <img class="icon" src="/images/section_icons/develop/tools/cassandraworkbench.png" aria-hidden="true" />
        <div class="title">Visual Studio Workbench</div>
      </div>
      <div class="body">
        Design and query YCQL database with help of generated templates, autocomplete, and inline code decorations.
      </div>
    </a>
  </div>

</div>

---
title: Third party tools
headerTitle: Third party tools
linkTitle: Third party tools
description: Interactive third party clients that you can use with YugabyteDB.
headcontent: Use popular third party clients with YugabyteDB
image: /images/section_icons/troubleshoot/troubleshoot.png
menu:
  stable:
    identifier: tools
    parent: reference
    weight: 2900
type: indexpage
showRightNav: true
---

Because YugabyteDB is compatible with PostgreSQL and Cassandra, you can use third-party clients to connect to your YugabyteDB clusters.

## Tools

<ul class="nav yb-pills">

  <li>
    <a href="superset/">
      <img src="/images/develop/tools/superset/superset-icon.png">
      Apache Superset
    </a>
  </li>

  <li>
    <a href="arctype/">
      <img src="/images/develop/tools/arctype/arctype-icon.png">
      Arctype
    </a>
  </li>

  <li>
    <a href="dbeaver-ysql/">
      <img src="/images/develop/tools/dbeaver-icon.png">
      DBeaver
    </a>
  </li>

  <li>
    <a href="dbschema/">
      <img src="/images/develop/tools/dbschema/dbschema-icon.png">
      DbSchema
    </a>
  </li>

  <li>
    <a href="metabase/">
      <img src="/images/section_icons/develop/ecosystem/metabase.png">
      Metabase
    </a>
  </li>

  <li>
    <a href="pgadmin/">
      <img src="/images/develop/tools/pgadmin-icon.png">
      pgAdmin
    </a>
  </li>

  <li>
    <a href="sql-workbench/">
      <img src="/images/develop/tools/sql-workbench.png">
      SQL Workbench/J
    </a>
  </li>

  <li>
    <a href="tableplus/">
      <img src="/images/section_icons/develop/tools/tableplus.png">
      TablePlus
    </a>
  </li>

</ul>

## YugabyteDB prerequisites

To use these tools with YugabyteDB, you should have a cluster up and running, and you will need to know the connection parameters required by the client to connect to your cluster.

To create a local cluster, follow the steps in [Quick start](/preview/quick-start/).

To create a cluster in YugabyteDB Managed, follow the steps in [Create a cluster](/preview/yugabyte-cloud/cloud-quickstart/). In addition, do the following:

- [Download the cluster certificate](/preview/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#download-your-cluster-certificate); YugabyteDB Managed requires the use of TLS
- [Add your computer to the cluster IP allow list](/preview/yugabyte-cloud/cloud-secure-clusters/add-connections/); this allows your computer to access the cluster

## Connection parameters

To connect, follow the client's configuration steps, and use the following values:

| Setting | Local installation | YugabyteDB Managed |
| :--- | :--- | :--- |
| Hostname | `localhost` or the IP address of a node | The cluster hostname as displayed on the cluster **Settings** tab |
| Port | `5433` (YSQL) `9042` (YCQL) | `5433` (YSQL) `9042` (YCQL) |
| Database | Database name (`yugabyte` is the default) | Database name (`yugabyte` is the default) |
| Username | `yugabyte` or `cassandra` | Database username (`admin` is the default) |
| Password | `yugabyte` or `cassandra`<br>Leave blank if [authentication is not enabled](../secure/enable-authentication/) | Database user password |

YugabyteDB Managed requires TLS. Use the root.ca certificate you downloaded for connections to YugabyteDB Managed clusters.

<!--
<div class="row">

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
    <a class="section-link icon-offset" href="tableplus/">
      <div class="head">
        <img class="icon" src="/images/section_icons/develop/tools/tableplus.png" aria-hidden="true" />
        <div class="title">TablePlus</div>
      </div>
      <div class="body">
        Unified developer console for querying databases.
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
    <a class="section-link icon-offset" href="visualstudioworkbench">
      <div class="head">
        <img class="icon" src="/images/section_icons/develop/tools/cassandraworkbench.png" aria-hidden="true" />
        <div class="title">Cassandra Workbench</div>
      </div>
      <div class="body">
        Visual Studio Code extension for querying Apache Cassandra databases.
      </div>
    </a>
  </div>

</div>
-->

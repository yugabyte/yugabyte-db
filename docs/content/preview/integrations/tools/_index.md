---
title: Third party tools
headerTitle: GUI clients
linkTitle: GUI clients
description: Interactive third party clients that you can use with YugabyteDB.
headcontent: Use popular third party clients with YugabyteDB
image: /images/section_icons/troubleshoot/troubleshoot.png
aliases:
  - /develop/tools/
  - /preview/develop/tools/
  - /preview/tools/
type: indexpage
showRightNav: true
cascade:
  unversioned: true
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

To create a cluster in YugabyteDB Aeon, follow the steps in [Create a cluster](/preview/yugabyte-cloud/cloud-quickstart/). In addition, do the following:

- [Download the cluster certificate](/preview/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#download-your-cluster-certificate); YugabyteDB Aeon requires the use of TLS
- [Add your computer to the cluster IP allow list](/preview/yugabyte-cloud/cloud-secure-clusters/add-connections/); this allows your computer to access the cluster

## Connection parameters

To connect, follow the client's configuration steps, and use the following values:

| Setting | Local installation | YugabyteDB Aeon |
| :--- | :--- | :--- |
| Hostname | `localhost` or the IP address of a node | The cluster hostname as displayed on the cluster **Settings** tab |
| Port | `5433` (YSQL) `9042` (YCQL) | `5433` (YSQL) `9042` (YCQL) |
| Database | Database name (`yugabyte` is the default) | Database name (`yugabyte` is the default) |
| Username | `yugabyte` or `cassandra` | Database username (`admin` is the default) |
| Password | `yugabyte` or `cassandra`<br>Leave blank if [authentication is not enabled](../../secure/enable-authentication/) | Database user password |

YugabyteDB Aeon requires TLS. Use the root.ca certificate you downloaded for connections to YugabyteDB Aeon clusters.

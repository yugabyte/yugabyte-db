---
title: Example applications
linkTitle: Example applications
description: Example applications for YugabyteDB Aeon.
headcontent: Example applications for YugabyteDB Aeon
image: /images/section_icons/index/develop.png
menu:
  preview_yugabyte-cloud:
    parent: yugabytedb-managed
    identifier: cloud-examples
    weight: 800
type: indexpage
---

The sample applications in this section provide advanced examples of connecting Spring, GraphQL, and YCQL Java applications to a YugabyteDB Aeon cluster.

To get started building applications for YugabyteDB Aeon, refer to [Build an application](../../tutorials/build-apps/).

Applications connect to and interact with YugabyteDB using API client libraries (also known as client drivers). Before you can connect an application, you need to install the correct driver. Because the YugabyteDB YSQL API is PostgreSQL-compatible, and the YCQL API has roots in the Apache Cassandra CQL, YugabyteDB supports many third-party drivers. For information on available drivers, refer to [Drivers](../../reference/drivers/).

{{<index/block>}}

<!--  {{<index/item
    title="Connect a Spring Data YugabyteDB application"
    body="Connect a Spring application implemented with Spring Data YugabyteDB."
    href="connect-application/"
    icon="/images/section_icons/develop/learn.png">}}
-->
  {{<index/item
    title="Connect a YCQL application"
    body="Connect a YCQL Java application."
    href="connect-ycql-application/"
    icon="/images/section_icons/develop/learn.png">}}

  {{<index/item
    title="Connect to Hasura Cloud"
    body="Connect a YugabyteDB Aeon cluster to a Hasura Cloud project."
    href="hasura-cloud/"
    icon="/images/section_icons/develop/real-world-apps.png">}}

<!--  {{<index/item
    title="Deploy a GraphQL application"
    body="Deploy a real-time polling application connected to YugabyteDB Aeon on Hasura Cloud."
    href="hasura-sample-app/"
    icon="/images/section_icons/develop/real-world-apps.png">}}
-->
{{</index/block>}}

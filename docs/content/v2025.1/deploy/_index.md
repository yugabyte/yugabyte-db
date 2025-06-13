---
title: Deploy
headerTitle: Deploy YugabyteDB
linkTitle: Deploy
description: How to deploy the YugabyteDB database to any public cloud or private data center or Kubernetes. Includes checklist and manual deployment options as well.
headcontent: Deploy to the public cloud, a private data center, or Kubernetes
menu:
  v2025.1:
    identifier: deploy
    parent: launch-and-manage
type: indexpage
---

{{< page-finder/head text="Deploy YugabyteDB" subtle="across different products">}}
  {{< page-finder/list icon="/icons/database-hover.svg" text="YugabyteDB" current="" >}}
  {{< page-finder/list icon="/icons/server-hover.svg" text="YugabyteDB Anywhere" url="../yugabyte-platform/create-deployments/" >}}
  {{< page-finder/list icon="/icons/cloud-hover.svg" text="YugabyteDB Aeon" url="/preview/yugabyte-cloud/cloud-basics/" >}}
{{< /page-finder/head >}}

{{<index/block>}}

  {{<index/item
    title="Checklist"
    body="Review system requirements, configuration details, and other important details when deploying YugabyteDB to production."
    href="checklist/"
    icon="fa-thin fa-list-check">}}

  {{<index/item
    title="Manual deployment"
    body="Deploy YugabyteDB manually in a private data center using basic administration commands."
    href="manual-deployment/"
    icon="fa-thin fa-helmet-safety">}}

  {{<index/item
    title="Public clouds"
    body="Automate and manually deploy YugabyteDB on public clouds."
    href="public-clouds/"
    icon="fa-thin fa-cloud">}}

  {{<index/item
    title="Kubernetes"
    body="Orchestrated deployment of YugabyteDB using open source as well as managed Kubernetes services."
    href="kubernetes/"
    icon="fa-thin fa-dharmachakra">}}

  {{<index/item
    title="Multi-DC deployments"
    body="Deploy across multiple data centers in 3DC or 2DC configurations."
    href="multi-dc/"
    icon="fa-thin fa-buildings">}}

{{</index/block>}}

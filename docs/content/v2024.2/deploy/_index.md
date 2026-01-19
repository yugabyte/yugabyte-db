---
title: Deploy
headerTitle: Deploy YugabyteDB
linkTitle: Deploy
description: How to deploy the YugabyteDB database to any public cloud or private data center or Kubernetes. Includes checklist and manual deployment options as well.
headcontent: Deploy to the public cloud, a private data center, or Kubernetes
menu:
  v2024.2:
    identifier: deploy
    parent: launch-and-manage
    weight: 10
type: indexpage
---

{{<index/block>}}

  {{<index/item
    title="Checklist"
    body="Review system requirements, configuration details, and other important details when deploying YugabyteDB to production."
    href="checklist/"
    icon="fa-thin fa-list-check">}}

{{</index/block>}}

{{<index/block>}}

  {{<index/item
    title="Single-DC deployments"
    body="Deploy YugabyteDB in a private data center."
    href="manual-deployment/"
    icon="fa-thin fa-helmet-safety">}}

  {{<index/item
    title="Multi-DC deployments"
    body="Deploy across multiple data centers in 3DC or 2DC configurations."
    href="multi-dc/"
    icon="fa-thin fa-buildings">}}

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

{{</index/block>}}

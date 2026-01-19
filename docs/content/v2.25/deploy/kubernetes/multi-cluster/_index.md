---
title: Deploy on multiple geo-distributed Kubernetes clusters
headerTitle: Deploy on multiple Kubernetes clusters
linkTitle: Multi-cluster
description: Deploy YugabyteDB on multiple geo-distributed Kubernetes clusters.
headcontent: Deploy YugabyteDB natively on multiple Kubernetes clusters.
menu:
  v2.25:
    identifier: deploy-kubernetes-mc
    parent: deploy-kubernetes
    weight: 623
type: indexpage
---

[Google Kubernetes Engine](https://cloud.google.com/kubernetes-engine/docs/concepts/types-of-clusters) can be configured to support global DNS across multiple Kubernetes clusters. For example, you can deploy a three-region YugabyteDB cluster on three Kubernetes clusters, each deployed in a different region, using the standard single-zone YugabyteDB Helm chart to deploy one third of the nodes in the database cluster in each of the three clusters.

{{<index/block>}}

  {{<index/item
    title="Google Kubernetes Engine (GKE)"
    body="Multi-cluster deployment on GKE."
    href="gke/helm-chart/"
    icon="/images/section_icons/deploy/gke.png">}}

{{</index/block>}}

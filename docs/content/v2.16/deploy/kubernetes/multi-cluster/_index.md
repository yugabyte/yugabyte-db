---
title: Deploy on multiple geo-distributed Kubernetes clusters
headerTitle: Deploy on multiple Kubernetes clusters
linkTitle: Multi-cluster
description: Deploy YugabyteDB on multiple geo-distributed Kubernetes clusters.
headcontent: Deploy YugabyteDB natively on multiple Kubernetes clusters.
image: /images/section_icons/deploy/kubernetes.png
menu:
  v2.16:
    identifier: deploy-kubernetes-mc
    parent: deploy-kubernetes
    weight: 623
type: indexpage
---

As described in the [Kubernetes documentation](https://kubernetes.io/docs/setup/best-practices/multiple-zones/), a single Kubernetes cluster can run only inside a single zone of a single region. This means workloads will be distributed across multiple nodes of a single Kubernetes cluster running in a single region. When geo-distributed SQL workloads such as YugabyteDB need to run on Kubernetes, the preferred approach is to create a single YugabyteDB cluster that spans multiple zones of a single Kubernetes cluster or multiple geo-distributed Kubernetes clusters.

[Google Kubernetes Engine](https://cloud.google.com/kubernetes-engine/docs/concepts/types-of-clusters) can be configured to support global DNS across multiple Kubernetes clusters. For example, you can deploy a three-region YugabyteDB cluster on three Kubernetes clusters, each deployed in a different region, using the standard single-zone YugabyteDB Helm chart to deploy one third of the nodes in the database cluster in each of the three clusters.

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="gke/helm-chart/">
      <div class="head">
        <img class="icon" src="/images/section_icons/deploy/gke.png" aria-hidden="true" />
        <div class="title">Google Kubernetes Engine (GKE)</div>
      </div>
      <div class="body">
        Multi-cluster deployment on GKE.
      </div>
    </a>
  </div>
</div>


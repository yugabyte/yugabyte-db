---
title: Deploy YugabyteDB on a multi-zone Kubernetes cluster
linkTitle: Multi-zone
headerTitle: Multi-zone Kubernetes
description: Deploy YugabyteDB on multi-zone Kubernetes
headcontent: Deploy YugabyteDB on a multi-zone Kubernetes cluster
menu:
  v2.25:
    identifier: deploy-kubernetes-mz
    parent: deploy-kubernetes
    weight: 622
type: indexpage
---

[Amazon Elastic Kubernetes Service](https://docs.aws.amazon.com/eks/latest/userguide/network_reqs.html) and [Google Kubernetes Engine](https://cloud.google.com/kubernetes-engine/docs/concepts/types-of-clusters) support multi-zone Kubernetes clusters automatically. The following instructions describe how to deploy a 3-zone YugabyteDB cluster on a 3-zone Kubernetes cluster. Both these deployments use the standard single-zone YugabyteDB Helm Chart to deploy one third of the nodes in the database cluster in each of the 3 zones.

{{<index/block>}}

  {{<index/item
    title="Amazon Elastic Kubernetes Service (Amazon EKS)"
    body="Multi-zone deployment on Amazon EKS."
    href="eks/helm-chart/"
    icon="/images/section_icons/deploy/amazon-eks.png">}}

  {{<index/item
    title="Google Kubernetes Engine (GKE)"
    body="Multi-zone deployment on GKE."
    href="gke/helm-chart/"
    icon="/images/section_icons/deploy/gke.png">}}

{{</index/block>}}

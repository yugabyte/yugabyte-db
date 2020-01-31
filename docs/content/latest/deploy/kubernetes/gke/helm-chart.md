---
title: Google Kubernetes Engine (GKE)
linkTitle: Google Kubernetes Engine (GKE)
description: Google Kubernetes Engine (GKE)
menu:
  latest:
    parent: deploy-kubernetes
    name: Google Kubernetes Engine
    identifier: k8s-gke-1
    weight: 623
aliases:
  - /latest/deploy/kubernetes/gke/
type: page
isTocNested: true
showAsideToc: true
---


<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="/latest/deploy/kubernetes/gke/helm-chart" class="nav-link active">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Helm chart
    </a>
  </li>
  <li >
    <a href="/latest/deploy/kubernetes/gke/statefulset-yaml" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      StatefulSet YAML
    </a>
  </li>
   <li >
    <a href="/latest/deploy/kubernetes/gke/statefulset-yaml-local-ssd" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      StatefulSet YAML with Local SSD
    </a>
  </li>
</ul>

Instructions specific to GKE are coming soon. Until then, refer to the [Open Source Kubernetes](../../oss/helm-chart/) instruc

tions.

## Configure cluster

### LoadBalancer for services

By default, the YugabyteDB Helm chart exposes only the master UI endpoint using LoadBalancer. If you want to expose the client API services (YSQL and YCQL) using LoadBalancer for your app to use, you could do that in couple of different ways.

If you want an individual LoadBalancer endpoint for each of the services, run the following command.

```sh
$ helm install yugabyte -f expose-all.yaml --namespace yb-demo --name yb-demo --wait
```

If you want to create a shared LoadBalancer endpoint for all the services, run the following command.

```sh
$ helm install yugabyte -f expose-all-shared.yaml --namespace yb-demo --name yb-demo --wait
```

You can also bring up an internal LoadBalancer (for either YB-Master or YB-TServer services), if required. Just specify the [annotation](https://kubernetes.io/docs/concepts/services-networking/service/#internal-load-balancer) required for your cloud provider. The following command brings up an internal LoadBalancer for the YB-TServer service in Google Cloud Platform.

```sh
$ helm install yugabyte -f expose-all.yaml --namespace yb-demo --name yb-demo \
  --set annotations.tserver.loadbalancer."cloud\.google\.com/load-balancer-type"=Internal --wait
```

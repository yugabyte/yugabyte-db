---
title: Amazon Elastic Kubernetes Service (EKS)
linkTitle: Amazon Elastic Kubernetes Service (EKS)
description: Amazon Elastic Kubernetes Service (EKS)
block_indexing: true
menu:
  v2.0:
    parent: deploy-kubernetes
    name: Amazon EKS
    identifier: k8s-eks-1
    weight: 622
type: page
isTocNested: true
showAsideToc: true
---


<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="/latest/deploy/kubernetes/eks/helm-chart" class="nav-link active">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Helm chart
    </a>
  </li>
</ul>

Detailed specific to Amazon EKS are coming soon. Refer to the [Kubernetes Open Source](../../oss/helm-chart/) instructions till then.

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

You can also bring up an internal LoadBalancer (for either YB-Master or YB-TServer services), if required. Just specify the [annotation](https://kubernetes.io/docs/concepts/services-networking/service/#internal-load-balancer) required for your cloud provider. The following command brings up an internal LoadBalancer for the YB-TServer service in AWS.

```sh
$ helm install yugabyte -f expose-all.yaml --namespace yb-demo --name yb-demo \
  --set annotations.tserver.loadbalancer."service\.beta\.kubernetes\.io/aws-load-balancer-internal"=0.0.0.0/0 --wait
```

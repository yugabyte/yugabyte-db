---
title: Helm configuration
linkTitle: Helm configuration
description: Helm configuration
block_indexing: true
menu:
  v1.3:
    identifier: helm-configuration
    parent: deploy-kubernetes
    weight: 621
isTocNested: true
showAsideToc: true
---

## Configure cluster

Instead of using the default values in the helm chart, you can also modify the configuration of the Yugabyte cluster according to your requirements. The following section shows the commands and tags that can be modified to achieve the desired configuration.

### CPU, memory, and replica count

The default values for the Helm chart are in the `helm/yugabyte/values.yaml` file. The most important ones are listed below. As noted in the Prerequisites section above, the defaults are set for a 3 nodes Kubernetes cluster each with 4 CPU cores and 15 GB RAM.

```
persistentVolume:
  count: 2
  storage: 10Gi
  storageClass: standard

resource:
  master:
    requests:
      cpu: 2
      memory: 7.5Gi
  tserver:
    requests:
      cpu: 2
      memory: 7.5Gi

replicas:
  master: 3
  tserver: 3

partition:
  master: 3
  tserver: 3
```

If you want to change the defaults, you can use the command below. You can even do `helm install` instead of `helm upgrade` when you are installing on a Kubernetes cluster with configuration different than the defaults.

```sh
$ helm upgrade --set resource.tserver.requests.cpu=8,resource.tserver.requests.memory=15Gi yb-demo ./yugabyte
```

Replica count can be changed using the command below. Note only the tservers need to be scaled in a Replication Factor 3 cluster which keeps the masters count at 3.

```sh
$ helm upgrade --set replicas.tserver=5 yb-demo ./yugabyte
```

### LoadBalancer for services

By default, the YugabyteDB helm chart exposes only the master ui endpoint using LoadBalancer. If you wish to expose also the ycql and yedis services via LoadBalancer for your app to use, you could do that in couple of different ways.

If you want individual LoadBalancer endpoint for each of the services (YCQL, YEDIS), run the following command.

```sh
$ helm install yugabyte -f expose-all.yaml --namespace yb-demo --name yb-demo --wait
```

If you want to create a shared LoadBalancer endpoint for all the services (YCQL, YEDIS), run the following command.

```sh
$ helm install yugabyte -f expose-all-shared.yaml --namespace yb-demo --name yb-demo --wait
```

You can also bring up an internal LoadBalancer (for either the masters' or the tservers' service) if required. Just specify the [annotation](https://kubernetes.io/docs/concepts/services-networking/service/#internal-load-balancer) required for your cloud provider. The following commands brings up an internal LoadBalancer for the tserver service(s) in the respective providers.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="#aks" class="nav-link active" id="aks-tab" data-toggle="tab" role="tab" aria-controls="aks" aria-selected="true">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      AKS
    </a>
  </li>
  <li>
    <a href="#gke" class="nav-link" id="gke-tab" data-toggle="tab" role="tab" aria-controls="gke" aria-selected="false">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      GKE
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="aks" class="tab-pane fade show active" role="tabpanel" aria-labelledby="aks-tab">
    {{% includeMarkdown "public-cloud/aks.md" /%}}
  </div>
  <div id="gke" class="tab-pane fade" role="tabpanel" aria-labelledby="gke-tab">
    {{% includeMarkdown "public-cloud/gke.md" /%}}
  </div>
</div>

### Storage class

In case you want to use a storage class other than the standard class for your deployment, provision the storage class and then pass in the name of the class while running the helm install command.

```sh
$ helm install yugabyte --namespace yb-demo --name yb-demo --set storage.master.storageClass=<desired storage class>,storage.tserver.storageClass=<desired storage class> --wait
```

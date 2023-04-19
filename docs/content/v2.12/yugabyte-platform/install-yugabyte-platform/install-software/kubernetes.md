---
title: Install Yugabyte Platform software - Kubernetes
headerTitle: Install Yugabyte Platform software - Kubernetes
linkTitle: Install software
description: Install Yugabyte Platform software in your Kubernetes environment.
menu:
  v2.12_yugabyte-platform:
    parent: install-yugabyte-platform
    identifier: install-software-2-kubernetes
    weight: 77
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../default/" class="nav-link">
      <i class="fa-solid fa-cloud"></i>Default</a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link active">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>Kubernetes</a>
  </li>

  <li>
    <a href="../airgapped/" class="nav-link">
      <i class="fa-solid fa-link-slash"></i>Airgapped</a>
  </li>

  <li>
    <a href="../openshift/" class="nav-link">
      <i class="fa-solid fa-cubes"></i>OpenShift</a>
  </li>

</ul>

## Install Yugabyte Platform on a Kubernetes Cluster

You install Yugabyte Platform on a Kubernetes cluster as follows:

1. Create a namespace by executing the following `kubectl create namespace` command:

    ```sh
    kubectl create namespace yb-platform
    ```

1. Apply the Yugabyte Platform secret that you obtained from [Yugabyte](https://www.yugabyte.com/platform/#request-trial-form) by running the following `kubectl create` command:

    ```sh
    kubectl create -f yugabyte-k8s-secret.yml -n yb-platform
    ```

    Expect the following output notifying you that the secret was created:

    ```output
    secret/yugabyte-k8s-pull-secret created
    ```

1. Run the following `helm repo add` command to clone the [YugabyteDB charts repository](https://charts.yugabyte.com/):

    ```sh
    helm repo add yugabytedb https://charts.yugabyte.com
    ```

    A message similar to the following should appear:

    ```output
    "yugabytedb" has been added to your repositories
    ```

    To search for the available chart version, run this command:

    ```sh
    helm search repo yugabytedb/yugaware --version {{<yb-version version="v2.12" format="short">}}
    ```

    The Helm Chart version and App version will be displayed:

    ```output
    NAME                 CHART VERSION  APP VERSION  DESCRIPTION
    yugabytedb/yugaware  {{<yb-version version="v2.12" format="short">}}         {{<yb-version version="v2.12" format="build">}}  YugaWare is YugaByte Database's Orchestration a...
    ```

1. Run the following `helm install` command to install the Yugabyte Platform (`yugaware`) Helm chart:

    ```sh
    helm install yw-test yugabytedb/yugaware --version {{<yb-version version="v2.12" format="short">}} -n yb-platform --wait
    ```

1. Optionally, set the TLS version for Nginx frontend by using `ssl_protocols` operational directive in the Helm installation, as follows:

    ```sh
    helm install yw-test yugabytedb/yugaware --version {{<yb-version version="v2.12" format="short">}} -n yb-platform --wait --set tls.sslProtocols="TLSv1.2"
    ```

1. Use the following command to check the service:

    ```sh
    kubectl get svc -n yb-platform
    ```

    The following output should appear:

    ```output
    NAME                  TYPE           CLUSTER-IP     EXTERNAL-IP    PORT(S)                       AGE
    yw-test-yugaware-ui   LoadBalancer   10.111.241.9   34.93.169.64   80:32006/TCP,9090:30691/TCP   2m12s
    ```

## Customize Yugabyte Platform

You can customize Yugabyte Platform on a Kubernetes cluster in a number of ways, such as the following:

- You can change CPU and memory resources by executing a command similar to the following:

  ```sh
  helm install yw-test yugabytedb/yugaware -n yb-platform \
    --set yugaware.resources.requests.cpu=2 \
    --set yugaware.resources.requests.memory=4Gi \
    --set yugaware.resources.limits.cpu=2 \
    --set yugaware.resources.limits.memory=4Gi \
    --set prometheus.resources.requests.mem=6Gi \
    --version {{<yb-version version="v2.12" format="short">}}
  ```

- You can disable the public-facing load balancer by providing the annotations to Yugabyte Platform service for disabling that load balancer. Since every cloud provider has different annontations for doing this, refer to the following documentation:

  - For Google Cloud, see [GKE](https://cloud.google.com/kubernetes-engine/docs/how-to/internal-load-balancing).
  - For Azure, see [AKS](https://docs.microsoft.com/en-us/azure/aks/internal-lb).
  - For AWS, see [EKS](https://docs.aws.amazon.com/eks/latest/userguide/load-balancing.html).

  \
  For example, for a GKE version earlier than 1.17, you would run a command similar to the following:

  ```sh
  helm install yw-test yugabytedb/yugaware -n yb-platform \
  --version {{<yb-version version="v2.12" format="short">}} \
  --set yugaware.service.annotations."cloud\.google\.com\/load-balancer-type"="Internal"
  ```

## Delete the Helm Installation of Yugabyte Platform

To delete the Helm installation, run the following command:

```sh
helm uninstall yw-test -n yb-platform
```

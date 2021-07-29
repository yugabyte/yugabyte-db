---
title: Install Yugabyte Platform software - Kubernetes
headerTitle: Install Yugabyte Platform software - Kubernetes
linkTitle: Install software
description: Install Yugabyte Platform software in your Kubernetes environment.
menu:
  latest:
    parent: install-yugabyte-platform
    identifier: install-software-2-kubernetes
    weight: 77
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/latest/yugabyte-platform/install-yugabyte-platform/install-software/default" class="nav-link">
      <i class="fas fa-cloud"></i>
      Default
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/install-yugabyte-platform/install-software/kubernetes" class="nav-link active">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li >
    <a href="/latest/yugabyte-platform/install-yugabyte-platform/install-software/airgapped" class="nav-link">
      <i class="fas fa-unlink"></i>
      Airgapped
    </a>
  </li>

<li >
    <a href="/latest/yugabyte-platform/install-yugabyte-platform/install-software/openshift" class="nav-link">
      <i class="fas fa-cubes"></i> OpenShift </a>
  </li>

</ul>

## Install Yugabyte Platform on a Kubernetes Cluster

You install Yugabyte Platform on a Kubernetes cluster as follows:

1. Create a namespace by executing the following `kubectl create namespace` command:

    ```sh
    kubectl create namespace yb-platform
    ```

2. Apply the Yugabyte Platform secret that you obtained from [Yugabyte](https://www.yugabyte.com/platform/#request-trial-form) by running the following `kubectl create` command:

    ```sh
    $ kubectl create -f yugabyte-k8s-secret.yml -n yb-platform
    ```

    Expect the following output notifying you that the secret was created:

    ```
    secret/yugabyte-k8s-pull-secret created
    ```

3. Run the following `helm repo add` command to clone the [YugabyteDB charts repository](https://charts.yugabyte.com/):

    ```sh
    $ helm repo add yugabytedb https://charts.yugabyte.com
    ```

    A message similar to the following should appear:

    ```
    "yugabytedb" has been added to your repositories
    ```

    To search for the available chart version, run this command:

    ```sh
    $ helm search repo yugabytedb/yugaware -l
    ```

    The latest Helm Chart version and App version will be displayed:

    ```
    NAME               	CHART VERSION	APP VERSION	DESRIPTION
    yugabytedb/yugabyte	2.3.3        	2.3.3.0	YugabyteDB is the high-performance distributed ..
    ```

4. Run the following `helm install` command to install Yugabyte Platform (`yugaware`) Helm chart:

    ```sh
    helm install yw-test yugabytedb/yugaware --version 2.3.3 -n yb-platform --wait
    ```

5. Optionally, set the TLS version for Nginx frontend by using `ssl_protocols` operational directive in the Helm installation, as follows:

    ```sh
    helm install yw-test yugabytedb/yugaware --version 2.3.3 -n yb-platform --wait --set tls.sslProtocols="TLSv1.2"
    ```

6. Use the following command to check the service:

    ```sh
    kubectl get svc -n yb-platform
    ```
    The following output should appear:

    ```
    NAME                  TYPE           CLUSTER-IP     EXTERNAL-IP    PORT(S)                       AGE
    yw-test-yugaware-ui   LoadBalancer   10.111.241.9   34.93.169.64   80:32006/TCP,9090:30691/TCP   2m12s
    ```

## Customization

1. To change CPU & memory resources:

  ```sh
  helm install yw-test yugabytedb/yugaware -n yb-platform \
    --set yugaware.resources.requests.cpu=2 \
    --set yugaware.resources.requests.memory=4Gi \
    --set yugaware.resources.limits.cpu=2 \
    --set yugaware.resources.limits.memory=4Gi
  ```

2. To disable the internet/public facing LB.

  Provide the annotations to YW service for disabling the Public facing LB. Every cloud has different annontations to disable the LB. Use the following docs links to know more.

  1. [GKE](https://cloud.google.com/kubernetes-engine/docs/how-to/internal-load-balancing)
  2. [AKS](https://docs.microsoft.com/en-us/azure/aks/internal-lb)
  3. [EKS](https://docs.aws.amazon.com/eks/latest/userguide/load-balancing.html)

  *Example-*

  For GKE lower than v1.17

  ```sh
  helm install yw-test yugabytedb/yugaware -n yb-platform \
    --set yugaware.service.annotations."cloud\.google\.com\/load-balancer-type"="Internal"
  ```

## Delete the Helm Installation of Yugabyte Platform

To delete the Helm installation, run the following command:

```sh
helm uninstall yw-test -n yb-platform
```

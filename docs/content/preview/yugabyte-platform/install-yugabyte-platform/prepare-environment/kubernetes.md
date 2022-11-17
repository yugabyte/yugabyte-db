---
title: Prepare the Kubernetes environment
headerTitle: Prepare the Kubernetes environment
linkTitle: Prepare the environment
description: Prepare the Kubernetes environment for YugabyteDB Anywhere.
aliases:
  - /preview/deploy/enterprise-edition/prepare-environment/
menu:
  preview_yugabyte-platform:
    parent: install-yugabyte-platform
    identifier: prepare-environment-4-kubernetes
    weight: 55
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../aws/" class="nav-link">
      <i class="fa-brands fa-aws" aria-hidden="true"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="../gcp/" class="nav-link">
       <i class="fa-brands fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

  <li>
    <a href="../azure/" class="nav-link">
      <i class="icon-azure" aria-hidden="true"></i>
      &nbsp;&nbsp; Azure
    </a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link active">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

<li>
    <a href="../openshift/" class="nav-link">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      OpenShift
    </a>
 </li>

  <li>
    <a href="../on-premises/" class="nav-link">
      <i class="fa-solid fa-building" aria-hidden="true"></i>
      On-premises
    </a>
  </li>

</ul>

<br>The YugabyteDB Anywhere Helm chart has been tested using the following software versions:

- Kubernetes 1.20 or later.
- Helm 3.4 or later.
- Ability to pull YugabyteDB Anywhere Docker image from [Quay.io](https://quay.io/) repository


Before installing the YugabyteDB Admin Console, verify that you have the following:

- A Kubernetes cluster configured with [Helm](https://helm.sh/).
- A Kubernetes node with minimum 4 CPU core and 15 GB RAM can be allocated to YugabyteDB Anywhere.
- A Kubernetes secret obtained from [Yugabyte](https://www.yugabyte.com/platform/#request-trial-form).

To confirm that `helm` is configured correctly, run the following command:

```sh
helm version
```

The output should be similar to the following:

```output
version.BuildInfo{Version:"v3.2.1", GitCommit:"fe51cd1e31e6a202cba7dead9552a6d418ded79a", GitTreeState:"clean", GoVersion:"go1.13.10"}
```

To be able to make use of the YugabeteDB Anywhere [node metrics](../../../troubleshoot/universe-issues/#node), specifically the ones related to CPU, you need to install the [kube-state-metrics](https://github.com/kubernetes/kube-state-metrics) version 1.9 add-on in your Kubernetes cluster.

Since this add-on might already be installed and running, you should perform a check by executing the following command:

```sh
kubectl get svc kube-state-metrics -n kube-system
```

If the add-on is not available, install it into the `kube-system` namespace by exectuting the following commands as a user of a service account with appropriate roles:

```sh
helm repo add prometheus-community https://prometheus-community.github.io/helm-charts
```

```sh
helm install -n kube-system --version 2.13.2 kube-state-metrics prometheus-community/kube-state-metrics
```

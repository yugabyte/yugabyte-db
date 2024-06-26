---
title: Prepare the Kubernetes environment
headerTitle: Prepare the Kubernetes environment
linkTitle: Prepare the environment
description: Prepare the Kubernetes environment for YugabyteDB Anywhere.
menu:
  v2.14_yugabyte-platform:
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

The YugabyteDB Anywhere Helm chart has been tested using the following software versions:

- Kubernetes 1.18 or later.
- Helm 3.4 or later.
- Ability to pull YugabyteDB Anywhere Docker image from quay.io repository


Before installing the YugabyteDB Admin Console, verify you have the following:

- A Kubernetes cluster configured with [Helm](https://helm.sh/).
- A Kubernetes node with minimum 4 CPU core and 15 GB RAM can be allocated to YugabyteDB Anywhere.
- A Kubernetes secret obtained from Yugabyte Support.

To confirm that `helm` is configured correctly, run the following command:

```sh
$ helm version
```

The output should be similar to the following:

```output
version.BuildInfo{Version:"v3.2.1", GitCommit:"fe51cd1e31e6a202cba7dead9552a6d418ded79a", GitTreeState:"clean", GoVersion:"go1.13.10"}
```

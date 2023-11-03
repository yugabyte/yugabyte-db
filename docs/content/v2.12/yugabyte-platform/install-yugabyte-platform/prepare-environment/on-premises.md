---
title: Prepare the private cloud (on-premise) environment
headerTitle: Prepare the private cloud (on-premise) environment
linkTitle: Prepare the environment
description: Prepare the private cloud (on-premise) environment for Yugabyte Platform.
menu:
  v2.12_yugabyte-platform:
    identifier: prepare-environment-5-private-cloud
    parent: install-yugabyte-platform
    weight: 55
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../aws" class="nav-link">
      <i class="fa-brands fa-aws" aria-hidden="true"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="../gcp" class="nav-link">
       <i class="fa-brands fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

  <li>
    <a href="../azure" class="nav-link">
      <i class="icon-azure" aria-hidden="true"></i>
      &nbsp;&nbsp; Azure
    </a>
  </li>

  <li>
    <a href="../kubernetes" class="nav-link">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li>
    <a href="../openshift" class="nav-link">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      OpenShift
    </a>
  </li>

  <li>
    <a href="../on-premises" class="nav-link active">
      <i class="fa-solid fa-building" aria-hidden="true"></i>
      On-premises
    </a>
  </li>

</ul>

To run a Yugabyte universe on a private cloud, you need to prepare one computer to run Yugabyte Platform. You then use Yugabyte Platform to install and manage the nodes that will make up the universe.

The platform node has the following requirements:

* Hardware requirements
  * Nodes: 1 Virtual Machine
  * Cores: 8 Cores
  * RAM: 16 GB
* Storage Disk:  100GB (minimum) (confirm SSD)
* Docker Engine: supported version 19.03.n. If not installed, see Installing Docker in [airgapped](https://www.replicated.com/docs/kb/supporting-your-customers/installing-docker-in-airgapped/).
* The following ports should be open on the Yugabyte Platform host:
  * 8800 – HTTP access to the Replicated UI
  * 80 – HTTP access to the Yugabyte Platform console
  * 443 - HTTPs access to the Yugabyte Platform console
  * 22 – SSH
  * 9090 - Prometheus metrics

  For more information on ports used by YugabyteDB, refer to [Default ports](../../../../reference/configuration/default-ports).

* Ability to connect from the Yugabyte Platform host to all YugabyteDB data nodes.

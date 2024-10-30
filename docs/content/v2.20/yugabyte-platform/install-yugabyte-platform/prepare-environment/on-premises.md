---
title: Prepare the private cloud (on-premise) environment
headerTitle: Cloud prerequisites
linkTitle: Cloud prerequisites
description: Prepare the private cloud (on-premise) environment for YugabyteDB Anywhere.
headContent: Prepare your on-premises environment for YugabyteDB Anywhere
menu:
  v2.20_yugabyte-platform:
    identifier: prepare-environment-5-private-cloud
    parent: install-yugabyte-platform
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
    <a href="../kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

<li>
    <a href="../openshift/" class="nav-link">
      <i class="fa-brands fa-redhat" aria-hidden="true"></i>
      OpenShift
    </a>
 </li>

  <li>
    <a href="../on-premises/" class="nav-link active">
      <i class="fa-solid fa-building" aria-hidden="true"></i>
      On-premises
    </a>
  </li>

</ul>

To run a YugabyteDB on a private cloud, you need to prepare one computer to run YugabyteDB Anywhere. This computer is in turn used to install and manage the nodes onto which you will deploy YugabyteDB universes.

For information on the requirements for the node running YugabyteDB Anywhere, refer to [YBA prerequisites](../../prerequisites/installer/).

For information on preparing the nodes onto which you will deploy universes, refer to [Prepare nodes for on-premises deployment](../../prepare-on-prem-nodes/).

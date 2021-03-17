---
title: Prepare the OpenShift environment
headerTitle: Prepare the OpenShift environment
linkTitle: Prepare the environment
description: Prepare the OpenShift environment for Yugabyte Platform
aliases:
  - /latest/deploy/enterprise-edition/prepare-environment/
menu:
  latest:
    parent: install-yugabyte-platform
    identifier: prepare-environment-1-OpenShift
    weight: 55
isTocNested: false
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="/latest/yugabyte-platform/install-yugabyte-platform/prepare-environment/aws" class="nav-link">
      <i class="icon-aws" aria-hidden="true"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/install-yugabyte-platform/prepare-environment/gcp" class="nav-link">
       <i class="fab fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/install-yugabyte-platform/prepare-environment/azure" class="nav-link">
      <i class="icon-azure" aria-hidden="true"></i>
      &nbsp;&nbsp; Azure
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/install-yugabyte-platform/prepare-environment/kubernetes" class="nav-link active">
      <i class="icon-aws" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

<li>
    <a href="/latest/yugabyte-platform/install-yugabyte-platform/prepare-environment/openshift" class="nav-link active">
      <i class="fab fa-building" aria-hidden="true"></i>
      OpenShift
    </a>
 </li>

  <li>
    <a href="/latest/yugabyte-platform/install-yugabyte-platform/prepare-environment/on-premises" class="nav-link">
      <i class="icon-aws" aria-hidden="true"></i>
      On-premises
    </a>
  </li>

</ul>

To prepare the environment for OpenShift, you start by provisioning the OpenShift cluster. 

The recommended OpenShift Container Platform (OCP) version is 4.6, with backward compatibility assumed but not guaranteed.

You should have 18 vCPU and 32 GB of memory available for testing Yugabyte Platform. This can be three or more nodes equivalent to Google Platform's n1-standard-8 (8 vCPU, 30 GB memory).

For more information on provisioning clusters, see the following: 

- [Configuring a GCP project](https://docs.openshift.com/container-platform/4.6/installing/installing_gcp/installing-gcp-account.html) 
- [Installing a cluster quickly on GCP](https://docs.openshift.com/container-platform/4.6/installing/installing_gcp/installing-gcp-default.html#prerequisites)

In addition, ensure that you have the following: 

- The latest oc binary in your path. For more information, see [Installing the OpenShift CLI](https://docs.openshift.com/container-platform/4.6/cli_reference/openshift_cli/getting-started-cli.html#installing-openshift-cli).
- The latest kubectl 1.19.7 binary in your path. See [Install and Set Up kubectl](https://kubernetes.io/docs/tasks/tools/install-kubectl/) for more information, or create a kubectl symlink pointing to oc.
- An admin user with the [cluster-admin](https://docs.openshift.com/container-platform/4.6/authentication/using-rbac.html#default-roles_using-rbac) ClusterRole bound to it. Depending on your configuration, this user might be kube:admin.
- An authenticated user in the cluster which can create new projects.

For testing purposes, you may configure an HTPasswd provider, as described in [Configuring an HTPasswd identity provider](https://docs.openshift.com/container-platform/4.6/authentication/identity_providers/configuring-htpasswd-identity-provider.html) and, specifically, in [Creating an HTPasswd file using Linux](https://docs.openshift.com/container-platform/4.6/authentication/identity_providers/configuring-htpasswd-identity-provider.html#identity-provider-creating-htpasswd-file-linux_configuring-htpasswd-identity-provider) and [Configuring identity providers using the web console](https://docs.openshift.com/container-platform/4.6/authentication/identity_providers/configuring-htpasswd-identity-provider.html#identity-provider-configuring-using-the-web-console_configuring-htpasswd-identity-provider).


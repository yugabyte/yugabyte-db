---
title: YugabyteDB Anywhere
headerTitle: YugabyteDB Anywhere
linkTitle: YugabyteDB Anywhere
headcontent: Self-managed Database-as-a-Service
description: YugabyteDB delivered as a private database-as-a-service for enterprises.
menu:
  v2.25_yugabyte-platform:
    name: "Overview"
    parent: yugabytedb-anywhere
    identifier: overview-yp
    weight: 10
type: indexpage
breadcrumbDisable: true
---

YugabyteDB Anywhere is a self-managed database-as-a-service offering from YugabyteDB that allows you to deploy and operate YugabyteDB universes at scale.

Use YugabyteDB Anywhere to automate the deployment and management of YugabyteDB in your preferred environments (spanning on-prem, in the public cloud, and in Kubernetes) and topologies (single- and multi-region). [Learn more](./yba-overview/)

## Install YugabyteDB Anywhere

{{< sections/3-boxes >}}
  {{< sections/3-box-card
    title="Prepare"
    description="Prepare your infrastructure, including cloud permissions, networking, and servers."
    buttonText="Learn more"
    buttonUrl="prepare/"
  >}}

  {{< sections/3-box-card
    title="Install"
    description="Install YugabyteDB Anywhere on any environment, including Kubernetes, public cloud, or private cloud."
    buttonText="Learn more"
    buttonUrl="install-yugabyte-platform/"
  >}}

  {{< sections/3-box-card
    title="Create providers"
    description="Create the provider configurations that you will use to deploy universes."
    buttonText="Learn more"
    buttonUrl="configure-yugabyte-platform/"
  >}}
{{< /sections/3-boxes >}}

## Use YugabyteDB Anywhere

{{< sections/3-boxes >}}
  {{< sections/3-box-card
    title="Deploy universes"
    description="Deploy multi-region, multi-zone, and multi-cloud universes."
    buttonText="Deploy"
    buttonUrl="create-deployments/"
  >}}

  {{< sections/3-box-card
    title="Manage universes"
    description="Modify universes and their nodes, upgrade YugabyteDB software."
    buttonText="Manage"
    buttonUrl="manage-deployments/"
  >}}

  {{< sections/3-box-card
    title="Back up universes"
    description="Configure storage, back up and restore universe data."
    buttonText="Back up"
    buttonUrl="back-up-restore-universes/"
  >}}
{{< /sections/3-boxes >}}

## Additional resources

{{< sections/3-boxes >}}
  {{< sections/3-box-card
  title="Yugabyte University"
  description="Take free courses on YugabyteDB Anywhere Operations."
  buttonText="Get started"
  buttonUrl="https://university.yugabyte.com/collections/administrators"
  >}}

  {{< sections/3-box-card
  title="Automation"
  description="Automate tasks using the API, CLI, Terraform, and more."
  buttonText="Automation"
  buttonUrl="anywhere-automation/"
  >}}

  {{< sections/3-box-card
  title="Support"
  linkText1="Contact Support"
  linkUrl1="https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360001955891"
  linkTarget1="_blank"
  linkText2="Join our community"
  linkUrl2="https://inviter.co/yugabytedb"
  linkTarget2="_blank"
  >}}

{{< /sections/3-boxes >}}

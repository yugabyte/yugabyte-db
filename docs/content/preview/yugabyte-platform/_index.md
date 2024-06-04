---
title: YugabyteDB Anywhere
headerTitle: YugabyteDB Anywhere
linkTitle: YugabyteDB Anywhere
headcontent: Self-managed Database-as-a-Service
description: YugabyteDB delivered as a private database-as-a-service for enterprises.
menu:
  preview_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: overview-yp
    weight: 10
type: indexpage
breadcrumbDisable: true
resourcesIntro: Quick Links
resources:
  - title: What's new
    url: /preview/releases/yba-releases/
  - title: FAQ
    url: /preview/faq/yugabyte-platform/
  - title: yugabyte.com
    url: https://www.yugabyte.com/anywhere/
  - title: University
    url: https://university.yugabyte.com/collections/administrators/
---

YugabyteDB Anywhere (YBA) is a self-managed database-as-a-service offering from YugabyteDB that allows you to deploy and operate YugabyteDB universes at scale.

Use YugabyteDB Anywhere to automate the deployment and management of YugabyteDB in your preferred environments (spanning on-prem, in the public cloud, and in Kubernetes) and topologies (single- and multi-region).

{{< sections/2-boxes >}}
  {{< sections/bottom-image-box
    title="Install"
    description="Install YugabyteDB Anywhere on any environment, including Kubernetes, public cloud, or private cloud."
    buttonText="Learn more"
    buttonUrl="install-yugabyte-platform/"
    imageAlt="Yugabyte cloud" imageUrl="/images/homepage/yugabyte-in-cloud.svg"
  >}}

  {{< sections/bottom-image-box
    title="Configure"
    description="Configure YugabyteDB Anywhere for deploying universes on various cloud providers."
    buttonText="Learn more"
    buttonUrl="configure-yugabyte-platform/"
    imageAlt="Locally Laptop" imageUrl="/images/homepage/locally-laptop.svg"
  >}}
{{< /sections/2-boxes >}}

## Use YugabyteDB Anywhere

{{< sections/3-boxes >}}
  {{< sections/3-box-card
    title="Deploy"
    description="Deploy multi-region, multi-zone, and multi-cloud universes."
    buttonText="Deploy"
    buttonUrl="create-deployments/"
  >}}

  {{< sections/3-box-card
    title="Manage"
    description="Modify universes and their nodes, upgrade YugabyteDB software."
    buttonText="Manage"
    buttonUrl="manage-deployments/"
  >}}

  {{< sections/3-box-card
    title="Back up"
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
  description="Automate tasks using the YugabyteDB Anywhere Terraform provider."
  buttonText="Automation"
  buttonUrl="anywhere-automation/"
  >}}

  {{< sections/3-box-card
  title="Support"
  linkText1="Contact Support"
  linkUrl1="https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360001955891"
  linkTarget1="_blank"
  linkText2="Join our community"
  linkUrl2="https://communityinviter.com/apps/yugabyte-db/register"
  linkTarget2="_blank"
  >}}

{{< /sections/3-boxes >}}

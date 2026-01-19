---
title: Manual deployment of YugabyteDB clusters
headerTitle: Single data center deployments
linkTitle: Single-DC deployments
description: Deploy YugabyteDB manually in a single region or private data center using basic administration commands.
headcontent: Deploy YugabyteDB in a single region or private data center
menu:
  v2.25:
    identifier: deploy-manual-deployment
    parent: deploy
    weight: 20
type: indexpage
---

This section describes generic deployment of a YugabyteDB cluster in a single region or data center with a multi-zone/multi-rack configuration. Note that single zone configuration is a special case of multi-zone where all placement related flags are set to the same value across every node.

For AWS deployments specifically, a [step-by-step guide](../public-clouds/aws/manual-deployment/) to deploying a YugabyteDB cluster is also available. These steps can be adapted for on-premises deployments or deployments in other clouds.

{{<index/block>}}

  {{<index/item
    title="1. System configuration"
    body="Configure various system parameters such as ulimits correctly to run YugabyteDB."
    href="system-config/"
    icon="fa-thin fa-gear">}}

  {{<index/item
    title="2. Install software"
    body="Install the YugabyteDB software on each of the nodes."
    href="install-software/"
    icon="fa-thin fa-wrench">}}

  {{<index/item
    title="3. Deploy YugabyteDB"
    body="Start YugabyteDB on the nodes."
    href="start-yugabyted/"
    icon="fa-thin fa-rocket-launch">}}

  {{<index/item
    title="4. Verify deployment"
    body="Verify the deployment."
    href="verify-deployment-yugabyted/"
    icon="fa-thin fa-badge-check">}}

{{</index/block>}}

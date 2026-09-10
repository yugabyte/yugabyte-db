---
title: Manual deployment of YugabyteDB clusters
headerTitle: Manual deployment
linkTitle: Manual deployment
description: Deploy YugabyteDB manually in a single region or private data center using basic administration commands.
headcontent: Deploy YugabyteDB manually in a single region or private data center
menu:
  v2024.2:
    identifier: deploy-manual-deployment
    parent: deploy
    weight: 610
type: indexpage
---

This section covers the generic manual deployment of a YugabyteDB cluster in a single region or data center with a multi-zone/multi-rack configuration. Note that single zone configuration is a special case of multi-zone where all placement related flags are set to the same value across every node.

For AWS deployments specifically, a [step-by-step guide](../public-clouds/aws/manual-deployment/) to deploying a YugabyteDB cluster is also available. These steps can be adopted for on-premises deployments or deployments in other clouds.

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
    title="3. Start YB-Masters"
    body="Start the YB-Master service."
    href="start-masters/"
    icon="fa-thin fa-rocket-launch">}}

  {{<index/item
    title="4. Start YB-TServers"
    body="Start the YB-TServer service."
    href="start-tservers/"
    icon="fa-thin fa-server">}}

  {{<index/item
    title="5. Verify deployment"
    body="Verify the deployment."
    href="verify-deployment/"
    icon="fa-thin fa-badge-check">}}

{{</index/block>}}

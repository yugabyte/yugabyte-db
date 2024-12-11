---
title: YugabyteDB Anywhere node software requirements
headerTitle: Software requirements for on-premises nodes
linkTitle: On-premises provider
description: Software requirements for on-premises provider nodes.
headContent: Prepare a VM for deploying universes on-premises
menu:
  preview_yugabyte-platform:
    identifier: software-on-prem
    parent: server-nodes-software
    weight: 20
type: docs
---

When deploying database clusters using an on-premises provider, YugabyteDB Anywhere (YBA) relies on you to manually create the VMs and provide these pre-created VMs to YBA.

With the on-premises provider, you must provide to YBA one, three, five, or more VM(s) with the following installed:

- [Supported Linux OS](../#linux-os)
- [Additional software](../#additional-software)
- If you are not connected to the Internet, [additional software for airgapped](../#additional-software-for-airgapped-deployment)

## How to provision the nodes for use in a database cluster

After you have created the VMs, they must be provisioned with YugabyteDB and related software before they can be deployed in a universe.

How you provision nodes for use with an on-premises provider depends on the SSH access that you can grant YBA to provision nodes.

{{<lead link="../software-on-prem-legacy/">}}
Learn [how to choose](../software-on-prem-legacy/) which provisioning mode to use.
{{</lead>}}

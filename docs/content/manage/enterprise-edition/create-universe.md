---
date: 2016-03-09T19:56:50+01:00
title: Create Universe
weight: 72
---

## Create Universe
Universe is a cluster of YugaByte DB instances grouped together to perform as one logical distributed database. All instances belonging to a single Universe run on the same type of cloud provider node. 

If there are no universes created yet, the Dashboard page will look like the following.

![Dashboard with No Universes](/images/no-univ-dashboard.png)

Click on "Create Universe" to enter your intent for the universe. The **Provider**, **Regions** and **Instance Type** fields were initialized based on the [cloud providers configured](/deploy/enterprise-edition/configure-cloud-providers/). As soon as **Provider**, **Regions** and **Nodes** are entered, an intelligent Node Placement Policy kicks in to specify how the nodes should be placed across all the Availability Zones so that maximum availability is guaranteed. 

Here's how to create a universe on the [AWS](/deploy/enterprise-edition/configure-cloud-providers/#amazon-web-services) cloud provider.

![Create Universe on AWS](/images/create-univ.png)

Here's how a Universe in Pending state looks like.

![Dashboard with Pending Universe](/images/pending-univ-dashboard.png)


## Universe detail

![Detail for a Pending Universe](/images/pending-univ-detail.png)

## Tasks level tracking for a Universe 

![Tasks for a Pending Universe](/images/pending-univ-tasks.png)

## Nodes underlying a Universe

![Nodes for a Pending Universe](/images/pending-univ-nodes.png)
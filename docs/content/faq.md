---
date: 2016-03-09T20:08:11+01:00
title: Frequently Asked Questions
weight: 70
---

## Architecture 

## Product Editions 

## Operational 

### What are the OS requirements and permissions to run the YugaByte admin console?

All prerequisites for YugaWare, the YugaByte admin console, are listed [here](/enterprise-edition/deploy/#prerequisites)

### What are the OS requirements and permissions to run the YugaByte data nodes?

All prerequisites for the YugaByte data nodes are listed [here](/enterprise-edition/admin/#prerequisites-for-data-nodes)

### How are the build artifacts packaged and where are they stored?

The admin console software is packaged as a set of docker container images hosted on [Quay.io](https://quay.io/) container registry and managed by [Replicated](https://www.replicated.com/) management tool. Installation of the admin console starts with installing Replicated on a Linux host. Replicated installs the [docker-engine] (https://docs.docker.com/engine/), the docker container runtime, and then pulls its own container images the Replicated.com container registry. YugaWare then becomes a managed application of Replicated, which starts by pulling the YugaWare container images from Quay.io for the very first time. Replicated ensures that YugaWare remains highly available as well as allows for instant upgrades by simply pulling the incremental container images associated with a newer YugaWare release. Note that if the host running the admin console does not have Internet connectivity, then a fully air-gapped installation option is also available.

The data node software is packaged into the YugaWare application. YugaWare distributes and installs the data node software on the hosts identified to run the data nodes. Since it's already packaged into existing artifacts, the data node does not require any Internet connectivity.

### How does the admin console interact with the YugaByte data nodes?

The YugaWare admin console does a password-less ssh to interact with the data nodes. It needs to have the access key file (like a PEM file) uploaded into it via the UI. The setup on each of the data nodes to configure password-less ssh is documented [here](/deploy/#private-cloud-or-on-premises-data-centers).

A REST API is also exposed by the admin console to the end users in addition to the UI as another means of interacting with the data platform.

## Apache Cassandra Compatibility

## Redis Compatibility




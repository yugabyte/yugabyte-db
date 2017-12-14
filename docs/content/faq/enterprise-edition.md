---
title: Enterprise Edition
weight: 2490
---

## What is YugaWare?

YugaWare, shipped as a part of YugaByte Enterprise, is the Admin Console for YugaByte DB. It has a built-in orchestration and monitoring engine for deploying YugaByte DB in any public or private cloud.

## How does the installation work for YugaByte Enterprise?

YugaWare first needs to be installed on any machine. The next step is to configure YugaWare to work with public and/or private clouds. In the case of public clouds, Yugaware spawns the machines to orchestrate bringing up the data platform. In the case of private clouds, you add the nodes you want to be a part of the data platform into Yugaware. Yugaware would need ssh access into these nodes in order to manage them.

## What are the OS requirements and permissions to run YugaWare, the YugaByte admin console?

Prerequisites for YugaWare are listed [here](/deploy/enterprise-edition/admin-console/#prerequisites).

## What are the OS requirements and permissions to run the YugaByte data nodes?

Prerequisites for the YugaByte data nodes are listed [here](/deploy/enterprise-edition/admin/#prerequisites).

## How are the build artifacts packaged and where are they stored?

The admin console software is packaged as a set of docker container images hosted on [Quay.io](https://quay.io/) container registry and managed by [Replicated](https://www.replicated.com/) management tool. Installation of the admin console starts with installing Replicated on a Linux host. Replicated installs the [docker-engine] (https://docs.docker.com/engine/), the Docker container runtime, and then pulls its own container images the Replicated.com container registry. YugaWare then becomes a managed application of Replicated, which starts by pulling the YugaWare container images from Quay.io for the very first time. Replicated ensures that YugaWare remains highly available as well as allows for instant upgrades by simply pulling the incremental container images associated with a newer YugaWare release. Note that if the host running the admin console does not have Internet connectivity, then a fully air-gapped installation option is also available.

The data node software is packaged into the YugaWare application. YugaWare distributes and installs the data node software on the hosts identified to run the data nodes. Since it's already packaged into existing artifacts, the data node does not require any Internet connectivity.

## How does the Admin Console interact with the YugaByte data nodes?

The YugaWare Admin Console does a password-less ssh to interact with the data nodes. It needs to have the access key file (like a PEM file) uploaded into it via the UI. The setup on each of the data nodes to configure password-less ssh is documented [here](/deploy/#private-cloud-or-on-premises-data-centers).

A REST API is also exposed by the admin console to the end users in addition to the UI as another means of interacting with the data platform.

## Would we have access to the database machines that get spawned in public clouds?

Yes, you would have access to all machines spawned. The machines are spawned by YugaWare. YugaWare runs on your machine in your AWS region/datacenter. If you have configured YugaWare to work with any public cloud like AWS or GCP,  it will spawn YugaByte nodes using your credentials on your behalf. These machines run in your account, but are created and managed by YugaWare on your behalf. You can log on to these machines anytime, and YugaWare will additionally show you some stats graphed into a built in dashboard either per node or per universe.

## How many machines would I need to try out YugaByte DB against my load?

You would need:  

- One machine to install Yugaware on  

- Minimum as many data nodes as the replication factor. So just one machine for replication factor 1, and 3 machines in case of rf=3  

- A machine to run the load tests on  

Typically you can saturate a database machine (or three in case of replication factor 3) with just one large enough test machine running a synthetic load tester that has a light usage pattern. YugaByte ships some synthetic load-testers with the product which can simulate a few different workloads. For example, one load tester simulates a timeseries/IoT style workload and another does stock-ticker like workload. But if you have a load tester that emulates your planned usage pattern, nothing like it!

## Can we control the properties (such as VPC, IOPS, tenancy etc.) of the machines Yugaware is spinning up? 

Yes, you can control what Yugaware is spinning up. For example: 

- You can choose if Yugaware should spawn a new VPC with peering to the VPC on which application servers are running (to isolate the database machines into a separate VPC) AWS, or ask it to re-use an existing VPC.  

- You to choose dedicated IOPs EBS drives on AWS and specify the number of dedicated IOPS you need.  

In general, we are be able to fill the gaps quickly if we are missing some features. But as a catch all, Yugaware allows creating these machines out of band and import these as "on-premise" install.  
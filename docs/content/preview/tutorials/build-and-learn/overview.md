---
title: Build and Learn tutorial overview
headerTitle: Tutorial overview
linkTitle: Overview
description: Build and Learn tutorial overview
menu:
  preview_tutorials:
    identifier: overview
    parent: tutorials-build-and-learn
    weight: 1
type: docs
---

>**Welcome to the Story of YugaPlus**
>
>Our story concerns YugaPlus, a small streaming service startup, and describes their journey from a single server running PostgreSQL with a regional customer base to global business. Follow along as YugaPlus launches its service, migrates to YugabyteDB to scale in the cloud, and builds a low latency fault-tolerant service for its growing user base.

Welcome to the Build and Learn tutorial! You are going to learn the essential capabilities of YugabyteDB by following the story of YugaPlus, a scalable and fault-tolerant streaming platform where users watch their favorite movies, series, and live events.

Throughout the following five chapters, you'll gain practical experience in these areas:

* [Chapter 1: Debuting with PostgreSQL](../chapter1-debuting-with-postgres) - Deploy the first version of the YugaPlus movie recommendation service on PostgreSQL. You'll also understand how YugaPlus uses the PostgreSQL pgvector extension and the full-text search capabilities to provide movie recommendations.

* [Chapter 2: Scaling with YugabyteDB](../chapter2-scaling-with-yugabytedb) - Learn how YugabyteDB distributes data and workloads while migrating YugaPlus to a multi-node YugabyteDB cluster. Additionally, you'll witness YugabyteDB's PostgreSQL compatibility in action as the application is migrated without any code changes.

* [Chapter 3: Tolerating outages](../chapter3-tolerating-outages) - Discover how YugabyteDB handles various outages, including major cloud incidents. You'll transition the YugaPlus movie recommendation service to a multi-region cluster, upgrade to the YugabyteDB smart driver, and then attempt to disrupt the application's availability.

* [Chapter 4: Going geo-distributed](../chapter4-going-global) - Understand various design patterns for global applications to find the balance between availability and performance for your application workloads. Practice using the latency-optimized geo-partitioning pattern so that YugaPlus can achieve low latency reads and writes across distant locations.

* [Chapter 5: Offloading operations](../chapter5-going-cloud-native) - Migrate the YugaPlus streaming platform to YugabyteDB Managed to learn how to offload the management, maintenance, and operations of your database clusters.

Ready? Then let's move on to [Chapter 1](../chapter1-debuting-with-postgres)!

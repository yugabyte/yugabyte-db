---
title: Scaling with YugabyteDB
headerTitle: "Chapter 2: Scaling with YugabyteDB"
linkTitle: Scaling with YugabyteDB
description: Migrate from a single-server PostgreSQL instance to a distributed YugabyteDB cluster
menu:
  stable:
    identifier: chapter2-scaling-with-yugabytedb
    parent: tutorials-build-and-learn
    weight: 3
type: docs
---

The days went by, thousands of users signed up for the YugaPlus streaming service and started watching their favorites 24/7. Soon, the YugaPlus team noticed that the PostgreSQL database server was about to outgrow its storage and compute capacity. They had a choice of switching to a database instance with larger storage capacity and more CPUs. However, that would lead to a downtime during the migration and they would need to repeat this process again after reaching the capacity of the new database instance.

Eventually, the YugaPlus team decided to solve the scalability requirements by migrating to a multi-node YugabyteDB cluster that can scale out and up on demand...

In this chapter you'll learn:

* Postgres compatibility (same driver, no code level changes)
* Scalability (just start a three node cluster)
* YugabyteDB Voyager

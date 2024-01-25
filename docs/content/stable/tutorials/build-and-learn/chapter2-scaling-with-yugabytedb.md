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

{{< note title="YugaPlus - Time to Scale" >}}
The days passed, and the YugaPlus streaming service saw thousands of new users, all eagerly watching their favorite movies 24/7. It wasn't long before the YugaPlus team noticed a looming issue: their PostgreSQL database server was quickly approaching its limits in storage and compute capacity. They pondered upgrading to a larger database instance with increased storage and more CPUs. Yet, such an upgrade would not only cause downtime during the migration but also might become a recurring issue as capacity limits would eventually be reached again.

After careful consideration, the team decided to tackle these scalability challenges by migrating to a multi-node YugabyteDB cluster that could scale up and out on demand...
{{< /note >}}

In this chapter you'll learn:

* Postgres compatibility (same driver, no code level changes)
* Scalability (just start a three node cluster)
* YugabyteDB Voyager

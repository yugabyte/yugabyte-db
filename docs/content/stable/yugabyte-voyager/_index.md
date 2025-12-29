---
title: Migrate to YugabyteDB using Voyager
headerTitle: YugabyteDB Voyager
linkTitle: YugabyteDB Voyager
headcontent: Simplify migration from legacy and cloud databases to YugabyteDB
cascade:
  unversioned: true
description: YugabyteDB Voyager is a powerful open-source data migration engine that helps you migrate your database to YugabyteDB quickly and securely.
type: indexpage
aliases:
  - /stable/voyager/
  - /stable/tools/voyager/
menu:
  stable_yugabyte-voyager:
    name: "Overview"
    identifier: yugabyte-voyager
    parent: yugabytedb-voyager
    weight: 99
breadcrumbDisable: true
---

Use YugabyteDB Voyager to manage end-to-end database migration, including cluster preparation, schema migration, and data migration. Voyager safely migrates data from PostgreSQL, MySQL, and Oracle databases to YugabyteDB Aeon, YugabyteDB Anywhere, and the core open source database, YugabyteDB.

{{< sections/text-with-right-image
  title="Get Started"
  description="Install YugabyteDB Voyager and perform an end-to-end offline migration with our quick start guide."
  imageTransparent=true
  buttonText="Quick start"
  buttonUrl="quickstart/"
  imageTransparent=true
  imageAlt="YugabyteDB Voyager" imageUrl="/images/homepage/voyager-transparent.svg"
>}}

{{< sections/3-boxes >}}
  {{< sections/3-box-card
    title="Learn how it works"
    description="Learn about features, source and target databases, and the migration workflow."
    buttonText="Introduction"
    buttonUrl="introduction/"
  >}}

  {{< sections/3-box-card
    title="Migrate your data"
    description="Migrate your database and verify the results."
    buttonText="migrate"
    buttonUrl="migrate/"
  >}}

  {{< sections/3-box-card
    title="Tune performance"
    description="Tune parameters to make migration jobs run faster."
    buttonText="performance"
    buttonUrl="reference/performance/"
  >}}
{{< /sections/3-boxes >}}

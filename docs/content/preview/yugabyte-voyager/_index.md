---
title: Migrate to YugabyteDB using Voyager
headerTitle: YugabyteDB Voyager
linkTitle: YugabyteDB Voyager
headcontent: Simplify migration from legacy and cloud databases to YugabyteDB
cascade:
  unversioned: true
description: YugabyteDB Voyager is a powerful open-source data migration engine that helps you migrate your database to YugabyteDB quickly and securely.
type: indexpage
menu:
  preview_yugabyte-voyager:
    identifier: yugabyte-voyager
    parent: yugabytedb-voyager
    weight: 99
breadcrumbDisable: true
resourcesIntro: Quick Links
resources:
  - title: What's new
    url: /preview/yugabyte-voyager/release-notes/
  - title: Download
    url: https://download.yugabyte.com/migrate
  - title: yugabyte.com
    url: https://www.yugabyte.com/voyager/
---

Use YugabyteDB Voyager to manage end-to-end database migration, including cluster preparation, schema migration, and data migration. Voyager safely migrates data from PostgreSQL, MySQL, and Oracle databases to YugabyteDB Managed, YugabyteDB Anywhere, and the core open source database, YugabyteDB.

{{< sections/text-with-right-image
  title="Get Started"
  description="Install YugabyteDB Voyager on different operating systems (RHEL, Ubuntu, macOS), or via environments such as Docker or an Airgapped installation."
  imageTransparent=true
  buttonText="Install"
  buttonUrl="install-yb-voyager/"
  imageTransparent=true
  imageAlt="YugabyteDB Voyager" imageUrl="/images/homepage/voyager-transparent.svg"
>}}

{{< sections/3-boxes >}}
  {{< sections/3-box-card
    title="Learn how it works"
    description="Learn about features, source and target databases, and the migration workflow."
    buttonText="Overview"
    buttonUrl="overview/"
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
    buttonUrl="performance/"
  >}}
{{< /sections/3-boxes >}}

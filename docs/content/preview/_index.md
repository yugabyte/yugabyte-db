---
title: YugabyteDB
description: YugabyteDB documentation is the best source to learn the most in-depth information about the YugabyteDB database, YugabyteDB Managed, and YugabyteDB Anywhere.
headcontent: Open source cloud native distributed SQL database
weight: 1
type: indexpage
resourcesIntro: Quick Links
resources:
  - title: Migrate to YugabyteDB
    url: /preview/migrate/
  - title: Deploy
    url: /preview/deploy/
  - title: Manage
    url: /preview/manage/
  - title: Troubleshoot
    url: /preview/troubleshoot/
---

{{< sections/2-boxes >}}
  {{< sections/bottom-image-box
    title="Get Started locally on your Laptop"
    description="Download and install YugabyteDB on your laptop, create a cluster, and build a sample application."
    buttonText="Quick Start"
    buttonUrl="/preview/quick-start/"
    imageAlt="Locally Laptop" imageUrl="/images/homepage/locally-laptop.svg"
  >}}

  {{< sections/bottom-image-box
    title="Explore distributed SQL"
    description="Explore the features of distributed SQL, with examples."
    buttonText="Explore"
    buttonUrl="/preview/explore/"
    imageAlt="Yugabyte cloud" imageUrl="/images/homepage/yugabyte-in-cloud.svg"
  >}}
{{< /sections/2-boxes >}}

## Develop for YugabyteDB

{{< sections/3-boxes>}}
  {{< sections/3-box-card
    title="Build a Hello world application"
    description="Use your favorite programming language to build an application that connects to a YugabyteDB cluster."
    buttonText="Build"
    buttonUrl="/preview/develop/build-apps/"
  >}}

  {{< sections/3-box-card
    title="Connect using drivers and ORMs"
    description="Connect applications to your database using familiar third-party divers and ORMs and YugabyteDB Smart Drivers."
    buttonText="Connect"
    buttonUrl="/preview/drivers-orms/"
  >}}

  {{< sections/3-box-card
    title="Use familiar APIs"
    description="Get up to speed quickly using YugabyteDB's PostgreSQL-compatible YSQL and Cassandra-based YCQL APIs."
    buttonText="Develop"
    buttonUrl="/preview/api/"
  >}}

{{< /sections/3-boxes >}}

## Get under the hood

{{< sections/3-boxes>}}
  {{< sections/3-box-card
    title="Architecture"
    description="Learn how YugabyteDB achieves consistency and high availability."
    buttonText="Learn More"
    buttonUrl="/preview/architecture/"
  >}}

  {{< sections/3-box-card
    title="Secure"
    description="Secure YugabyteDB with authentication, authorization, and encryption."
    buttonText="Secure"
    buttonUrl="/preview/secure/"
  >}}

  {{< sections/3-box-card
    title="Configure"
    description="Configure core database services."
    buttonText="Configure"
    buttonUrl="/preview/reference/configuration/"
  >}}

{{< /sections/3-boxes >}}

<div class="row">

<!--
 <div class="col-12 col-md-6">
    <a class="section-link icon-offset" href="benchmark/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/high_performance.png" aria-hidden="true" />
        <div class="title">Benchmark</div>
      </div>
      <div class="body">
        Run performance, correctness, and data density benchmarks
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6">
    <a class="section-link icon-offset" href="reference/drivers/">
      <div class="head">
        <img class="icon" src="/images/section_icons/reference/connectors/ecosystem-integrations.png" aria-hidden="true" />
        <div class="title">Drivers and ORMs</div>
      </div>
      <div class="body">
        Drivers for powering applications with YugabyteDB
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6">
    <a class="section-link icon-offset" href="admin/">
      <div class="head">
        <img class="icon" src="/images/section_icons/index/admin.png" aria-hidden="true" />
        <div class="title">CLI reference</div>
      </div>
      <div class="body">
        Admin commands and utilities reference.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6">
    <a class="section-link icon-offset" href="reference/configuration">
      <div class="head">
        <img class="icon" src="/images/section_icons/reference/configuration/sample_apps.png" aria-hidden="true" />
        <div class="title">Configuration reference</div>
      </div>
      <div class="body">
        Configure YB-TServer and YB-Master services.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6">
    <a class="section-link icon-offset" href="reference/connectors/">
      <div class="head">
        <img class="icon" src="/images/section_icons/reference/connectors/ecosystem-integrations.png" aria-hidden="true" />
        <div class="title">Connectors</div>
      </div>
      <div class="body">
        Connectors for integrating with YugabyteDB
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6">
    <a class="section-link icon-offset" href="tools/">
      <div class="head">
        <img class="icon" src="/images/section_icons/troubleshoot/troubleshoot.png" aria-hidden="true" />
        <div class="articles">6 chapters</div>
        <div class="title">Third party tools</div>
      </div>
      <div class="body">
        GUI tools for developing and managing YugabyteDB databases
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6">
    <a class="section-link icon-offset" href="sample-data/">
      <div class="head">
        <img class="icon" src="/images/section_icons/sample-data/s_s1-sampledata-3x.png" aria-hidden="true" />
        <div class="articles">4 chapters</div>
        <div class="title">Sample datasets</div>
      </div>
      <div class="body">
        Sample datasets for use with YugabyteDB
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6">
    <a class="section-link icon-offset" href="releases/whats-new">
      <div class="head">
        <img class="icon" src="/images/section_icons/quick_start/install.png" aria-hidden="true" />
        <div class="title">Releases</div>
      </div>
      <div class="body">
        What's new in the latest and current stable releases
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6">
    <a class="section-link icon-offset" href="releases/earlier-releases/">
      <div class="head">
        <img class="icon" src="/images/section_icons/quick_start/install.png" aria-hidden="true" />
        <div class="title">Earlier releases</div>
      </div>
      <div class="body">
        Release notes for current and earlier releases
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6">
    <a class="section-link icon-offset" href="comparisons/">
      <div class="head">
        <img class="icon" src="/images/section_icons/index/comparisons.png" aria-hidden="true" />
        <div class="title">Comparisons</div>
      </div>
      <div class="body">
        Comparisons with common operational databases
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6">
    <a class="section-link icon-offset" href="faq/general">
      <div class="head">
        <img class="icon" src="/images/section_icons/introduction/core_features.png" aria-hidden="true" />
        <div class="title">FAQs</div>
      </div>
      <div class="body">
        Frequently asked questions about YugabyteDB, operations, API compatibility, and YugabyteDB Anywhere
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6">
    <a class="section-link icon-offset" href="/preview/contribute/">
      <div class="head">
        <img class="icon" src="/images/section_icons/index/introduction.png" aria-hidden="true" />
        <div class="title">Get involved</div>
      </div>
      <div class="body">
        Learn how you can become a contributor.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6">
    <a class="section-link icon-offset" href="/preview/contribute/core-database/">
      <div class="head">
        <img class="icon" src="/images/section_icons/index/introduction.png" aria-hidden="true" />
        <div class="title">Core database</div>
      </div>
      <div class="body">
        Contribute to the core database
      </div>
    </a>
  </div> -->

</div>

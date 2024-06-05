---
title: Build Multi-cloud Applications
headerTitle: Build multi-cloud applications
linkTitle: Build multi-cloud applications
description: Build applications that run on different clouds
headcontent: Build applications that run on different clouds
image: /images/section_icons/develop/api-icon.png
menu:
  preview:
    identifier: build-multicloud-apps
    parent: develop
    weight: 300
type: indexpage
showRightNav: true
---

Most organizations choose a single cloud provider (or private data centers) to deploy their applications. But this can lead to vendor lock-in, and the feature set and growth of your cloud provider can become a bottleneck to growth.

![Multi-cloud Yugabyte](/images/develop/multicloud/multicloud-global-spread.png)

## The need for multi-cloud

The main objective of adopting a multi-cloud strategy is to provide you with the flexibility to use the optimal computing environment for each specific workload. A multi-cloud strategy has a variety of benefits, including the following:

- **Avoid vendor lock-in**. Break free from the constraints of relying on a single provider and gain the freedom to build your infrastructure anywhere.
- **Application-specific optimization**. Align the specific features and capabilities of different clouds with the requirements of your applications. You can take into account factors such as speed, performance, reliability, geographical location, security, and compliance, thereby tailoring your cloud environment to best suit your unique needs.
- **Minimize cost**. Reduce costs by harnessing the benefits of optimal pricing and performance combinations offered by various cloud providers.
- **Higher availability**. An outage of one cloud provider need not mean an application outage, when traffic can be seamlessly redirected to another prepared cloud, ensuring uninterrupted operations.
- **Closer to users**. Not all cloud providers may have data centers close to your users in different geographies. By choosing data centers from different cloud providers, you can provide a better experience to all your users.
- **Data compliance**. Local data protection laws require citizen data to be stored in their country. If one cloud provider doesn't have a data center in a region you need, another provider might.
- **Flexibility**. If you aren't able to use your current cloud provider in a specific geography, being multi-cloud-ready makes the switch to another provider simpler.

## Build multi-cloud applications

While a multi-cloud approach offers numerous advantages, heightened management complexity and achieving consistent performance and reliability across multiple clouds present big challenges.

YugabyteDB is designed to address these challenges. Multi-cloud management capabilities have been integrated directly into [YugabyteDB](../../), [YugabyteDB Anywhere](../../yugabyte-platform/), and [YugabyteDB Managed](../../yugabyte-cloud/). This integration provides comprehensive visibility of your database across all your cloud environments, allowing you to monitor costs and usage, implement consistent security controls and policies, and seamlessly manage workloads.

### Setup

To understand how you can set up YugabyteDB across different clouds, see [Multi-cloud setup](./multicloud-setup).

### Deploy

After you set up your multi-cloud, you need to choose a suitable design pattern for your applications, depending on your needs for availability and data access. See [Global applications](../build-global-apps/) for more details.

## Hybrid cloud

The journey to public clouds from private data centers (_on-prem_) is not instantaneous. It takes a lot of time and planning. One of the first steps is to move a few select applications to the public cloud while maintaining the rest in private data centers.

This hybrid approach has become increasingly prevalent in modern infrastructure setups. During cloud migrations, organizations frequently adopt hybrid cloud implementations as they gradually and methodically transition their applications and data. Hybrid cloud environments enable the continued use of on-premises services while harnessing the benefits of flexible data storage and application access options provided by public cloud providers.

To understand how you can set up YugabyteDB in a hybrid cloud environment, see [Hybrid cloud](./hybrid-cloud).

## Migrate between clouds

Depending on the needs of your application or your organization, you might want to migrate from one cloud provider to another, or from your on-prem data center to a public cloud. This can be a daunting task, given the differences between various cloud providers.

YugabyteDB offers basic patterns to make this migration seamless. You can set up two separate universes and replicate from the old data center onto the new one. Or you can set up a [Global database](../build-global-apps/global-database) across all your data centers and then configure the database to use just a specific data center using data placement policies. For more details, see [Multi-cloud migration](./multicloud-migration).

## Learn more

{{<index/block>}}

{{<index/item
    title="Multi-cloud setup"
    body="Set up a YugabyteDB universe across AWS, Azure, and GCP."
    href="./multicloud-setup"
    icon="fa-brands fa-cloudflare">}}

{{<index/item
    title="Multi-cloud migration"
    body="Migrate your data from one cloud to another."
    icon="fa-solid fa-cloud-arrow-up"
    href="./multicloud-migration">}}

{{<index/item
    title="Hybrid cloud"
    body="Add a public cloud to your on-prem environment."
    icon="fa-brands fa-soundcloud"
    href="./hybrid-cloud">}}

{{</index/block>}}

---
title: Build  Multi-cloud Applications
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
---

Most organizations choose a single cloud provider (or private data centers) to deploy their applications. But this can lead to vendor lock-in and the feature set and growth of your cloud provider can become a bottleneck for the growth of your organization.

You can adopt a multi-cloud strategy and deploy your applications in two or more public cloud providers or opt for a combination of your private data centers and public cloud. But multi-cloud adds complexity and the overhead of managing different cloud providers.

![Multi-cloud Yugabyte](/images/develop/multicloud/multicloud-global-spread.png)

Let us see how multi-cloud would benefit your organization and understand how YugabyteDB helps you manage multi-cloud with ease.

## The need for multi-cloud

The main objective of adopting a multi-cloud strategy is to provide you with the flexibility to use the optimal computing environment for each specific workload. A multi-cloud strategy has a variety of benefits, including the following:

- **Avoid vendor lock-in**: Break free from the constraints of relying on a single provider and gain the freedom to build your infrastructure anywhere.
- **Application-specific optimization**: Align the specific features and capabilities of different clouds with the requirements of your applications. You can take into account factors such as speed, performance, reliability, geographical location, security, and compliance, thereby tailoring your cloud environment to best suit your unique needs.
- **Minimize cost**: Lower costs by harnessing the benefits of optimal pricing and performance combinations offered by various cloud providers.
- **Higher availability**: An outage of one cloud provider need not mean an application outage as you would be seamlessly redirected to another prepared cloud, ensuring uninterrupted operations.
- **Closer to users**: Not all cloud providers may have data centers close to your users in different geographies. By choosing data centers from different cloud providers, you can provide a better experience to all your users.
- **Data compliance**: Local data protection laws require data of their citizens to be placed in their country. One cloud provider may not have a data center in the regions you need, but another provider might.
- **Flexibility**: There might be scenarios where you may not be able to use your current cloud provider and would have to use another provider in a specific geography. In such cases, being multicloud-ready would make this move simpler.

## Building multicloud applications

While a multi-cloud approach offers numerous advantages, heightened management complexity and achieving consistent performance and reliability across multiple clouds present big challenges for organizations to overcome. 

### Setup

YugabyteDB has been designed to address these challenges from day one. Multicloud management capabilities have been integrated directly into all the 3 product offerings - [YugabyteDB](../../), [Yugabyte Anywhere](../../yugabyte-platform/) and [Yugabyte Managed](../../yugabyte-cloud/). This integration provides comprehensive visibility of your database across all your cloud environments, allowing you to monitor costs and usage, implement consistent security controls and policies, and seamlessly manage workloads. To understand how you can set up YugabyteDB across different clouds see, [Multicloud setup](./multicloud-setup)

### Deploy

Once you have set up your multicloud, you need to choose a good design pattern for your applications as per the needs of your organization. Depending your needs for Availability & Data Access, we define several patterns for you to choose from. See [Global applications](../build-global-apps/) for more info.

## Hybrid cloud

The digital transformation journey to move to public clouds from private data centers (_on-prem_) is not instantaneous. It takes a lot of time and planning. One of the first steps for many organizations would be to have a few applications in the public cloud while still running many of their applications in private data centers.

The hybrid cloud approach has become increasingly prevalent in modern infrastructure setups. During cloud migrations, organizations frequently adopt hybrid cloud implementations as they gradually and methodically transition their applications and data. Hybrid cloud environments enable the continued use of on-premises services while harnessing the benefits of flexible data storage and application access options provided by public cloud providers.

To understand how you can set up YugabyteDB in a hybrid cloud environment see [Hybrid cloud](./hybrid-cloud).

## Migration between clouds

Depending on the needs of your application or your organization, you might want to migrate from one cloud provider to another, or from your on-prem data center to a public cloud. This could become a daunting task, given the differences between various cloud providers.

YugabyteDB offers simple patterns to make this migration seamless. You could set up two separate universes and replicate from the old data center onto the new one. Or you could set up a [Global database](../build-global-apps/global-database) across all your data centers and then configure the database to use just a specific data center using data placement policies. For more details, see [Multi-cloud migration](./migration)

## Learn more

{{<index/block>}}

{{<index/item
    title="Multi-cloud setup"
    body="Setup a YugabyteDB universe across AWS/GCP/Azure."
    href="./multicloud-setup"
    icon="fa-solid fa-equals">}}

{{<index/item
    title="Cloud migration"
    body="Migrate your data from one cloud to another."
    icon="fa-solid fa-equals"
    href="./multicloud-migration">}}

{{<index/item
    title="Hybrid cloud"
    body="Add a public cloud to your on-prem environment."
    icon="fa-brands fa-searchengin"
    href="./hybrid-cloud">}}

{{<index/item
    title="Bank of Anthos"
    body="Full fledged example of a Banking application using YugabyteDB on a multi-cloud setup."
    icon="fa-solid fa-music"
    href="https://github.com/yugabyte/bank-of-anthos">}}

{{</index/block>}}

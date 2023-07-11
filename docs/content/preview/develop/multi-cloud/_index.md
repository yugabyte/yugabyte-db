---
title: Build  Multicloud Applications
headerTitle: Build Multicloud Applications
linkTitle: Build multicloud applications
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

## TODO

- Cloud failure
- 2dc on Prem + 1 external cloud
- Migration from AWS to GCP , keep one as backup (xcluster)
- https://www.cockroachlabs.com/docs/stable/demo-automatic-cloud-migration.html
- Deploy across AWS, GCP, Azure
- Hybrid deployments
- Migrating between clouds
- Use maybe an example - Bank of Anthos

## Overview

Most organizations choose a single cloud provider(or private data centers) to deploy their applications. But this leads to vendor lock-in and the feature set and growth of your cloud provider can become a bottleneck for the growth of your organization.

You can adopt a multicloud strategy and deploy your applications in 2 or more public cloud providers or opt for a combination of your private data centers and public cloud. But multicloud brings in the huge overhead of managing different cloud providers.

![Multicloud Yugabyte](/images/develop/multicloud/multicloud-global-spread.png)

Let us see how multicloud would benefit your organization and understand how Yugabyte helps you manage multicloud with ease.

## The need for multicloud

The main objective of adopting a multicloud strategy is to provide you with the flexibility to utilize the optimal computing environment for each specific workload. Multicloud strategy has a variety of benefits.

- **Avoid vendor lock-in**: Break free from the constraints of relying on a single provider and snatch the freedom to build your infrastructure anywhere
- **Application-specific optimization**: Flexibility to align specific features and capabilities of different clouds with your application needs, considering factors such as speed, performance, reliability, geographical location, as well as security and compliance requirements, thereby tailoring your cloud environment to best suit your unique needs.
- **Minimize cost**: Lower costs by harnessing the benefits of optimal pricing and performance combinations offered by various cloud providers.
- **Higher Availability**: An outage of one cloud provider need not mean an application outage as you would be seamlessly redirected to another prepared cloud, ensuring uninterrupted operations.
- **Closer to users**: Not all cloud providers might have data centers closer to your users in different geographies. Choose data centers from different cloud providers that are closer to your users across the world and provide a better experience to all your users.
- **Data Compliance**: Local data protection laws require data of their citizens to be placed within their country. Your cloud provider may not have a data center in that region, but another provider might.

## Multicloud management

While a multicloud approach offers numerous advantages, heightened management complexity and achieving consistent performance and reliability across multiple clouds could become big challenges for organizations to overcome.

As the number of cloud providers you utilize increases, the complexity of managing your environment grows. Different public cloud vendors offer distinct features, services and APIs for managing their services. While it is possible to manage each environment separately, the majority of IT teams lack the necessary time and resources.

To address these challenges, multicloud management capabilities must be integrated directly into your cloud provider's products and solutions. This integration enables you to gain comprehensive visibility across all your cloud environments, monitor costs and usage, implement consistent security controls and policies, and seamlessly manage workloads.

## Hybrid cloud

The digital transformation journey to move to public clouds from private data centers(_on-prem_) is not instantaneous. It takes a lot of time and planning. One of the first steps for many organizations would be to have a few applications in the public cloud while still running many of their applications in private data centers(_on-prem_). This setup of mixed cloud environments of both public cloud and private data centers is typically referred to as **Hybrid cloud**.

{{<tip>}}
For more details, see [Hybrid cloud](./hybrid-cloud)
{{</tip>}}


A hybrid cloud approach has become increasingly prevalent in modern infrastructure setups. During cloud migrations, organizations frequently adopt hybrid cloud implementations as they gradually and methodically transition their applications and data. Hybrid cloud environments enable the continued use of on-premises services while harnessing the benefits of flexible data storage and application access options provided by public cloud providers.

## Migration

Depending on the needs of your application or the decision of your organization, you might want to migrate from one cloud provider to another or from your on-prem data center to a public cloud. This could become a daunting task, given the differences between various cloud providers. Yugabyte offers simple patterns to make this migration seamless.

{{<tip>}}
For more details, see [Multicloud migration](./migration)
{{</tip>}}

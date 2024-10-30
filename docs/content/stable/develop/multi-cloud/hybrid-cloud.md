---
title: Hybrid Cloud
headerTitle: Hybrid cloud
linkTitle: Hybrid cloud
description: Build applications that run across on-prem datacenters and public clouds
headcontent: Build applications that run across on-prem datacenters and public clouds
menu:
  stable:
    identifier: multicloud-hybrid-cloud
    parent: build-multicloud-apps
    weight: 300
type: docs
---

One of the first steps for many organizations in the digital modernization journey is to move a few applications to the public cloud while still running many of their applications in private data centers (_on-prem_). This setup of mixed cloud environments of both public cloud and private data centers is typically referred to as a **Hybrid cloud**.

During cloud migrations, organizations frequently adopt hybrid cloud implementations as they gradually and methodically transition their applications and data. Hybrid cloud environments enable the continued use of on-premises services while harnessing the benefits of flexible data storage and application access options provided by public cloud providers.

The following illustration shows a basic hybrid cloud, with two on-premises data centers and one public cloud.

![Hybrid Cloud](/images/develop/multicloud/hybridcloud-example.png)

## Benefits of hybrid cloud

A hybrid cloud approach is ideal for leveraging the scalability and presence offered by public clouds while keeping data on-premises to comply with data regulation laws. The following are a few key benefits of adopting a hybrid cloud:

- **Gradual modernization**: With a hybrid cloud, you have the flexibility to migrate applications to the cloud at your preferred pace, allowing you to modernize your technical infrastructure gradually over time.

- **Regulatory compliance**: Many industries have specific regulations regarding the operating location of applications and data. A hybrid cloud environment enables you to use both private and public clouds while adhering to regulatory requirements.

- **Legacy application support**: You may have legacy systems that are challenging to migrate to the cloud. Hybrid cloud allows you to accommodate these systems by keeping applications on-premises.

- **Edge computing capabilities**: Industries such as retail or telecommunications often demand low-latency edge computing, such as running applications in kiosks or network edge locations. With a hybrid approach, you can run select applications at the edge, meeting these specific needs.

- **Latest technologies**: Hybrid cloud models offer the opportunity to leverage cutting-edge technologies such as AI and machine learning without the need to expand or replace your current infrastructure.

By adopting a hybrid cloud approach, you can harness the benefits of both public and private clouds, tailoring your infrastructure to meet regulatory compliance, operational limitations, and latency-sensitive requirements.

## Hybrid cloud deployment patterns

Every hybrid cloud strategy has to be adapted to the specific needs of your organization, taking into account the workloads, compliance, and availability. Use the following patterns to map out a path to move applications to a public cloud.

For this example, assume you have two on-prem data centers in `us-west` and `us-east`, and you are adding a region in `us-central` on GCP.

### Move trivial workloads onto public cloud

One of the key benefits of a Hybrid cloud strategy is that you can slowly migrate applications from your on-prem data centers to the public cloud. The first phase of this process could be to move some applications to public cloud. For example, suppose you have two on-prem data centers with YugabyteDB deployed for your production and test applications as follows:

![Two On-Premises data centers with YugabyteDB](/images/develop/multicloud/hybridcloud-2-onprem.png)

As a part of your data center modernization and expansion, you could choose a public cloud provider in a region where you do not have a presence (for example, `us-central`) and move your testing application to that public cloud, as follows:

![Two On-Premises data centers and a Public Cloud with Yugabyte](/images/develop/multicloud/hybridcloud-move-testing-app.png)

{{<lead link="../../../manage/backup-restore/">}}
To replicate your production data onto your test cluster, see [Backup and Restore](../../../manage/backup-restore/).
{{</lead>}}

This move has multiple advantages:

- You get to test the new public data center without affecting your production systems.
- Instead of spending time and money to set up a data center in `us-central`, you leverage existing cloud infrastructure to add your presence in `us-central`.
- Adding a new public cloud to your environment and moving less important workloads to it opens up the possibility of running other important workloads in your on-prem data centers.

### Move resource-intensive workloads onto public cloud

After you have tested out the new public cloud you have added to your infrastructure, you can begin moving more important and resource-intensive applications (like machine-learning logic/stream processing, and so on) to the newly adopted public cloud.

![Two On-Premises data centers and a Public Cloud with Yugabyte](/images/develop/multicloud/hybridcloud-move-important-app.png)

The reason for moving resource-intensive applications to the public cloud is that it is easier to switch to powerful node instances that the public cloud provides. For example, [AWS Instance types](https://aws.amazon.com/ec2/instance-types/) and [GCP Instance Types](https://cloud.google.com/compute/docs/machine-resource).

### Spread applications across the hybrid cloud

After you become comfortable with managing the newly added public cloud and are happy with its performance, it would be a good choice to spread your primary database across both the on-prem data center and the public cloud, as shown in the following illustration:

![Global Database on Hybrid Cloud](/images/develop/multicloud/hybridcloud-global-database.png)

With this setup, you get a [Global Database](../../build-global-apps/global-database) that keeps your data consistent across your two on-prem data centers and the public cloud and enables access to your data for all the applications running in all your data centers. In this case, you can set your on-prem data center as the preferred zone-1 and the public cloud as the second preferred zone.

## Failover

As you have set the public cloud as the second preferred zone, were the primary on-prem data center in `us-west` to fail, the tablet followers in the GCP region in `us-central` would be promoted to leader and your applications can continue to serve your users without any data loss.

![Hybrid Cloud Failover](/images/develop/multicloud/hybridcloud-failover.png)

## Learn more

- [Replicate your data using backup and restore](../../../manage/backup-restore/)
- [Maintain a backup cluster in another region using Active-Active Single Master design pattern](../../build-global-apps/active-active-single-master/)
- [Multi-cloud setup](../multicloud-setup/)

---
title: What's new
linkTitle: What's new
description: Yugabyte Cloud release notes and known issues.
headcontent:
image: /images/section_icons/index/quick_start.png
menu:
  latest:
    identifier: cloud-release
    parent: yugabyte-cloud
    weight: 950
isTocNested: true
showAsideToc: true
---

## Releases

### December 16, 2021

#### New features

- Self service Virtual Private Cloud (VPC) networking. Use VPC networks to lower network latencies and make your application and database infrastructure more secure. Create VPCs in AWS or GCP and peer them with application VPCs in the same cloud provider. VPC networking is managed on the **VPC Network** tab of the **Network Access** page.
- Ability to enable pre-bundled extensions using the `CREATE EXTENSION` command. YugabyteDB includes [pre-bundled PostgreSQL extensions](../../explore/ysql-language-features/extensions/) that are tested to work with YSQL. Admin users now have additional permissions to allow them to enable these extensions in databases. (If you need to install a database extension that is not pre-bundled, contact [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431).)

### December 2, 2021

#### New features

- Additional performance metrics. The new cluster **Performance Metrics** tab features new metrics including YSQL and YCQL operations per second, YSQL and YCQL latency, network bytes per second, and more. Use these metrics to ensure the cluster configuration matches its performance requirements.
- Ability to review running queries using the **Live Queries** on the cluster **Performance** tab. Use this information to visually identify relevant database operations and evaluate query execution times.
- Ability to review slow YSQL queries using the **YSQL Slow Queries** on the cluster **Performance** tab. You can use this information to identify slower-running database operations, look at query execution times over time, and discover potential queries for tuning.

#### Database

- YugabyteDB updated to version 2.8. New clusters automatically use the new version. Existing clusters will be upgraded in the coming weeks.

### November 18, 2021

#### New features

- Support for auditing cloud activity using the new **Activity** tab on the **Admin** page. The tab provides a running audit of activity on your cloud, including backups, cluster creation and modification, changes to cloud users and their roles, billing changes, and allow list changes.
- Support for multiple Admin users on your cloud account, and Admin users can now change the role of existing users. You can also invite multiple users to your cloud at once, and assign them a role (Developer or Admin) when you invite them. You manage cloud users using the **Users** tab on the **Admin** page.
- Additional charts on the **Invoices** on the **Billing** tab, which break costs down by cluster and infrastructure (instance costs, storage, and data transfer) so that you can quickly evaluate your cloud costs.

#### Fixes

- Developer users can now use the cloud shell.

### October 5, 2021

#### New features

- The [Yugabyte Cloud Status](https://status.yugabyte.cloud/) page shows the current uptime status of Yugabyte Cloud and the [Yugabyte Support Portal](https://support.yugabyte.com/), along with maintenance notices and incident reports.
- Ability to review cluster activity using the new cluster **Activity** tab.

### September 15, 2021

#### New features

- Ability to [create clusters](../cloud-basics/create-clusters) suitable for production workloads. Yugabyte Cloud clusters support horizontal and vertical scaling, VPC peering, and scheduled and manual backups.
- Billing support. Set up a billing profile, manage payment methods, and review invoices on the [Billing](../cloud-admin/cloud-billing-profile) tab. (You must create a billing profile and add a payment method before you can create any clusters apart from your free cluster.)

### September 8, 2021

This release includes the following features:

- Free clusters (one per account)
- AWS and GCP cloud support
- IP allow lists for network security
- Cloud shell for running SQL queries from your browser
- YSQL and YCQL API support
- Multiple cloud users - invite additional users to your cloud
- Encryption at rest and in transit

## Cloud provider regions

The following **GCP regions** are available:

- Taiwan (asia-east1)
- Tokyo (asia-northeast1)
- Mumbai (asia-south1)
- Singapore (asia-southeast1)
- Sydney (australia-southeast1)
- Belgium (europe-west1)
- London (europe-west2)
- Frankfurt (europe-west3)
- Netherlands (europe-west4)
- Montreal (northamerica-northeast1)
- Sao Paulo (southamerica-east1)
- Iowa (us-central1)
- South Carolina (us-east1)
- N. Virginia (us-east4)
- Oregon (us-west1)
- Los Angeles (us-west2)
- Salt Lake City (us-west3)
- Las Vegas (us-west4)

The following **AWS regions** are available:

- Tokyo (ap-northeast-1)
- Mumbai (ap-south-1)
- Singapore (ap-southeast-1)
- Sydney (ap-southeast-2)
- Central (ca-central-1)
- Frankfurt (eu-central-1)
- Ireland (eu-west-1)
- London (eu-west-2)
- Paris (eu-west-3)
- Sao Paulo (sa-east-1)
- N. Virginia (us-east-1)
- Ohio (us-east-2)
- N. California (us-west-1)
- Oregon (us-west-2)

## Known issues

- **Tables** - In some instances in free clusters, the **Tables** tab will show all tables with a size of 0B.
- **Clusters** - No support for scaling vCPUs on single node clusters.
- **Metrics** - The **View Full Screen** option in charts on the cluster **Overview** and **Performance Metrics** pages does not work in some versions of Safari 14.0 (Big Sur).
- **Installing PostgreSQL Extensions** - For security reasons, the admin account used for the YugabyteDB database is not a super user. As a result, you cannot install extensions. If you need a database extension that is not [pre-bundled](../../explore/ysql-language-features/extensions/) with YugabyteDB added to a cluster, contact [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431) with the names of the cluster and extension, or reach out on Slack.

### Known issues in cloud shell

- At this time, we recommend running only a single cloud shell session. Running more than one session may produce unexpected results.
- If the cloud shell stops responding, close the browser tab and restart the cloud shell.
- Cloud shell is unavailable for clusters in a VPC.
- Cloud shell is unavailable during any edit and backup/restore operations. Wait until the operations are complete before you launch the shell.
- No support for keyboard shortcuts on Windows. Use the shortcut menu.
- No support for keyboard shortcuts in Firefox. Use the shortcut menu.

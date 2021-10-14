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

### October 5, 2021

#### New features

- The [Yugabyte Cloud Status](https://status.yugabyte.cloud/) page shows the current uptime status of Yugabyte Cloud and the [Yugabyte Support Portal](https://support.yugabyte.com/), along with maintenance notices and incident reports.
- Review cluster activity on the [Activity](../cloud-clusters/monitor-activity) tab.

### September 15, 2021

#### New features

- You can now [create Paid clusters](../cloud-basics/create-clusters). Paid clusters are suitable for production workloads, and support horizontal and vertical scaling, VPC peering, and scheduled and manual backups.
- Set up a billing profile and payment methods and review invoices on the [Billing](../cloud-admin/cloud-billing-profile) tab.

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
- **PostgreSQL Extensions** - For security reasons, the admin account used for the YugabyteDB database is not the super user. As a result, the admin account cannot install extensions, or enable extensions using the `CREATE EXTENSION` command. Support for installing and enabling extensions is planned. In the meantime, if you need database extensions added to a cluster, contact [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431) with the names of the cluster and extension, or reach out on Slack. 

### Known issues in cloud shell

- At this time, we recommend running only a single cloud shell session. Running more than one session may produce unexpected results.
- If the cloud shell stops responding, close the browser tab and restart the cloud shell.
- Cloud shell is unavailable for clusters with VPC peering
- Cloud shell is unavailable during any edit and backup/restore operations. Wait until the operations are complete before you launch the shell.
- No support for invited (Developer) users.
- No support for keyboard shortcuts on Windows. Use the shortcut menu.
- No support for keyboard shortcuts in Firefox. Use the shortcut menu.

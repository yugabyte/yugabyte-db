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

## v1.0 - September 8, 2021

This release includes the following features:

- Free clusters (one per account)
- AWS and GCP cloud support
- IP allow lists for network security 
- Cloud shell for running SQL queries from your browser
- YSQL and YCQL API support
- Multiple cloud users - invite additional users to your cloud
- Encryption at rest and in transit

The following regions are supported.

|         GCP Region      |  Name          | AWS Region     |  Name |
|-------------------------|----------------|----------------|---------------|
| asia-east1              | Taiwan         | | |
| asia-northeast1         | Tokyo          | ap-northeast-1 | Tokyo |
| asia-south1             | Mumbai         | ap-south-1     | Mumbai |
| asia-southeast1         | Singapore      | ap-southeast-1 | Singapore |
| australia-southeast1    | Sydney         | ap-southeast-2 | Sydney |
| europe-west1            | Belgium        | eu-west-1      | Ireland |
| europe-west2            | London         | eu-west-2      | London |
| europe-west3            | Frankfurt      | eu-central-1   | Frankfurt |
| europe-west4            | Netherlands    | eu-west-3      | Paris |
| northamerica-northeast1 | Montreal       | ca-central-1   | Central |
| southamerica-east1      | Sao Paulo      | sa-east-1      | Sao Paulo |
| us-central1             | Iowa           | us-east-2      | Ohio |
| us-east1                | South Carolina | | |
| us-east4                | N. Virginia    | us-east-1      | N. Virginia |
| us-west1                | Oregon         | us-west-2      | Oregon |
| us-west2                | Los Angeles    | us-west-1      | N. California |
| us-west3                | Salt Lake City | | |
| us-west4                | Las Vegas      | | |

### Limitations

- Free clusters only. Paid clusters coming soon.

### Known issues

- **Cloud Shell** - No support for Firefox.
- **Cloud Shell** - The cloud shell cannot display a greater number of lines of output than the page size. For example, if you run a query that returns more rows than there are lines of output in the current browser view, the results will not display.
- **Cloud Shell** - No support for invited (Developer) users. 
- **Tables** - In some instances in free clusters, the **Tables** tab will show all tables with a size of 0B.

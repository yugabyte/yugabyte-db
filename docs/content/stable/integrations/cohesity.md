---
title: Cohesity
linkTitle: Cohesity
description: Use Cohesity to back up and restore your YugabyteDB database.
menu:
  stable_integrations:
    identifier: cohesity
    parent: integrations-platforms
    weight: 571
type: docs
---

[Cohesity](https://www.cohesity.com/products/smartfiles/) provides a comprehensive, highly scalable, and flexible backup solution that fits the data protection needs of any size organization.

By using Cohesity SmartFiles as a backup target for YugabyteDB universes, you can take advantage of Cohesity's many powerful features, including:

- Speed – Recover from cyberattacks many times faster compared to other systems.
- Security – Improve your security posture, detect threats, and protect data.
- Scale – Secure and protect YugabyteDB and your other data sources on a single platform, even at a petabyte scale.
- Simplicity – Run Yugabyte backup and recovery workflows from a unified control plane and set of APIs (just as you do for the rest of your data estate).
- Smarts – Gain business and operational insights from your data with advanced AI capabilities.

## Set up Cohesity as your backup target

Because YugabyteDB Anywhere supports S3-compatible storage, integrating Cohesity with YugabyteDB Anywhere is a straightforward three step process:

1. Create a Cohesity SmartFiles S3 View.

    To use Cohesity as backup storage for a YugabyteDB universe, you first need to create a Cohesity View using the Object Services category, choose a QoS policy, and configure the View for S3. For this solution, Cohesity recommends having inline deduplication and inline compression enabled on the Storage Domain in which you create the View. See the Cohesity documentation for more information.

    To access Cohesity SmartFiles S3 Object Service as a backup storage from YugabyteDB Anywhere, you will also need the access key and secret key of the S3 bucket owner.

1. Create an S3-compatible backup configuration in YugabyteDB Anywhere.

    {{< note title="S3-compatible storage requires S3 path style access" >}}
  By default, the option to use S3 path style access is not available.

  To enable the feature in YugabyteDB Anywhere, set the **Enable Path Access Style for Amazon S3** Global Runtime Configuration option (config key `yb.ui.feature_flags.enable_path_style_access`) to true. Refer to [Manage runtime configuration settings](../../yugabyte-platform/administer-yugabyte-platform/manage-runtime-config/). Note that only a Super Admin user can modify Global configuration settings.
    {{< /note >}}

    YugabyteDB Anywhere supports any S3-compatible storage. To create a backup configuration that uses your Cohesity S3 View, navigate to **Integrations** > **Backup** > **Amazon S3**, click **Create S3 Backup**, and enter the following details:

    - Configuration Name - provide a meaningful name for your storage configuration.
    - Access Key and Access Secret - provide the access key and secret of the user who created the S3 bucket on the Cohesity Cluster.
    - S3 Bucket - enter the name of the S3 bucket created on Cohesity which you plan to use as backup storage.
    - S3 Bucket Host Base - enter the Cohesity cluster URL with port, for example, `https://cohesity.com:3000`.
    - S3 Path Style Access - set this option to true.

    Click **Save** when you are done.

    For more information on backup configurations in YugabyteDB Anywhere, refer to [Configure backup storage](../../yugabyte-platform/back-up-restore-universes/configure-backup-storage/#amazon-s3).

1. Configure your YugabyteDB universe to use the backup configuration that you created as you would normally. For more information, refer to [Back up and restore universes](../../yugabyte-platform/back-up-restore-universes/back-up-universe-data/).

## Learn more

- [Protecting YugabyteDB Data With Cohesity](https://www.yugabyte.com/blog/protecting-yugabytedb-with-cohesity/)
- [Cohesity Data Cloud now protects and secures YugabyteDB workloads](https://www.cohesity.com/blogs/cohesity-data-cloud-now-protects-and-secures-yugabytedb-workloads/)

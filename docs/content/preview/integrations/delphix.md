---
title: Delphix
linkTitle: Delphix
description: Use Delphix to virtualize your YugabyteDB database.
menu:
  preview_integrations:
    identifier: Delphix
    parent: integrations-platforms
    weight: 571
type: docs
---

Delphix is a data operations company that enables organizations to modernize their data infrastructure by providing software solutions for data management and delivery. Some of its key features include data virtualization, data masking, data replication, and data management across on-premises, cloud, and hybrid environments.

Delphix offers the following key capabilities as part of its integration with YugabyteDB:

- Select Connector for continuous data (Data Virtualization): Delphix can virtualize YugabyteDB databases by ingesting data to the Delphix platform, and create lightweight copies of YugabyteDB that can be provisioned quickly.

- Select Connector for continuous compliance (Data Masking): Delphix provides data masking capabilities for YugabyteDB to protect sensitive information in the database, and also comply with data privacy regulations.

The resulting virtual copies can then be used for different use cases such as development, testing, analytics, reporting, and so on.

For more information, refer to [Yugabyte data sources](https://cd.delphix.com/docs/latest/yugabyte-data-sources) in the Delphix documentation.

{{<note title="Support for yb_backup.py script in Delphix integrations">}}
The Delphix documentation refers to the `yb_backup.py` script, which is used to take a snapshot of the YugabyteDB production database for ingestion by Delphix. This script can be called directly or via the [Create a backup API](https://api-docs.yugabyte.com/docs/yugabyte-platform/2d87d8d6901ad-create-a-backup-v2). The script has been deprecated in YugabyteDB, _except_ when used for integrating with Delphix. The `yb_backup.py` script is officially supported by YugabyteDB for Delphix integration purposes.
{{</note>}}

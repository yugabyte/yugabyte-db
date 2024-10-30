---
title: Troubleshoot cloud provider configuration issues
headerTitle:
linkTitle: Cloud provider configuration issues
description: Troubleshoot issues encountered when configuring cloud providers for YugabyteDB Anywhere.
menu:
  v2.20_yugabyte-platform:
    identifier: cloud-provider-config-issues
    parent: troubleshoot-yp
    weight: 30
type: docs
---

You might encounter issues during configuration of cloud providers for YugabyteDB Anywhere.

If you have problems while troubleshooting, contact {{% support-platform %}}.

## Azure cloud provider configuration problems

You can diagnose and remedy a failure that occurred when [configuring Azure cloud provider](../../configure-yugabyte-platform/set-up-cloud-provider/azure/) as follows:

- Navigate to **Tasks** on the left-side menu.

- Sort the tasks by their status.

- Find your task of type **Create Provider** among the **Failed** tasks and click the corresponding **See Details**.

- On the **Task details** page shown in the following illustration, click **Expand** to view the diagnostic information:

  ![Azure configuration failure](/images/yp/platform-azure-prepare-cloud-env-6.png)

Typically, the failure is caused by your subscription not having enough quota on Azure to create the specific size VM cores in a specific region. To resolve the issue, increase the quota limit by following instructions provided in [Increase VM-family vCPU quotas](https://docs.microsoft.com/en-us/azure/azure-portal/supportability/per-vm-quota-requests).

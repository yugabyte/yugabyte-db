---
title: API Keys
headerTitle:
linkTitle: API Keys
description: Manage your cloud API keys.
menu:
  preview:
    identifier: cloud-admin-apikeys
    parent: cloud-admin
    weight: 20
isTocNested: true
showAsideToc: true
---

YugabyteDB Managed provides a REST API so that you can manage clusters programmatically. The API uses bearer token authentication, and each request requires a secret key, called an API key. Admin users can generate API keys for your account.

Safeguard your API Key by storing it in a secure location with strong encryption. Revoke keys if security has been compromised. Don't embed keys in code. Applications that contain keys can be decompiled to extract keys, or de-obfuscated from on-device storage. API keys can also be compromised if committed to a code repository.

API keys are not stored in YugabyteDB Managed; store them securely. Revoke keys that are lost or compromised.

You must be signed in as an Admin user to create and revoke API keys.

The **API Keys** tab of the **Admin** page displays a list of API keys created for your account that includes the key name, key status, the user that created the key, and the date it was created, last used, and expires.

![VPCs](/images/yb-cloud/cloud-vpc.png)

To view API key details, select an API key in the list to display the **API Key Details** sheet.

To create an API key:

1. On the **API Keys** tab of the **Admin** page, click **Create API Key**.

1. Enter a name and description for the key.

1. Set the key expiration, or select **Never expire** to create a key without an expiration.

1. Click **Generate Key**.

1. Click the Copy icon to copy the key.

1. Store the key in a secure location.

1. Click **Done**.

To revoke an API key, click **Revoke** for the API key in the list you want to revoke. You can also revoke an API key by clicking  **Revoke API Key** in the **API Key Details** sheet.

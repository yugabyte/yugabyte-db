---
title: API keys
headerTitle: API keys
linkTitle: API keys
description: Manage API keys for use with YugabyteDB Managed automation tools including API, CLI, and Terraform provider.
headcontent: Use API keys to authenticate with automation tools
aliases:
  - /preview/yugabyte-cloud/cloud-admin/cloud-admin-apikeys/
menu:
  preview_yugabyte-cloud:
    identifier: managed-apikeys
    parent: managed-automation
    weight: 10
type: docs
---

YugabyteDB Managed provides a REST API so that you can manage clusters programmatically. The API uses bearer token authentication, and each request requires a secret key, called an API key.

API keys are not stored in YugabyteDB Managed. Safeguard them by doing the following:

- Store API keys in a secure location with strong encryption, such as a password manager.
- Revoke keys that are lost or compromised.
- Don't embed keys in code. Applications that contain keys can be de-compiled to extract keys, or de-obfuscated from on-device storage. API keys can also be compromised if committed to a code repository.

API keys are [role-specific](../../managed-security/managed-roles/); for example, keys assigned a Developer role can only be used to perform developer-level tasks using the API.

The **API Keys** tab under **Access Control** on the **Security** page displays a list of API keys created for your account that includes the key name, key status, the user that created the key, and the date it was created, last used, and expires.

![API Keys](/images/yb-cloud/managed-admin-apikeys.png)

To view API key details, select an API key in the list to display the **API Key Details** sheet.

## Create an API key

To create an API key:

1. Navigate to **Admin > Access Control > API Keys**, and click **Create API Key**.

1. Enter a name and description for the key.

1. Choose a [role](../../managed-security/managed-roles/) for the API Key.

1. Set the key expiration or select **Never expire** to create a key without an expiration.

1. Click **Generate Key**.

1. Click the Copy icon to copy the key and store the key in a secure location.

    {{< warning title="Important" >}}

The key is only displayed one time; it is not available in YugabyteDB Managed after you click **Done**. Store keys in a secure location. If you lose a key, revoke it and create a new one.

    {{< /warning >}}

1. Click **Done**.

## Revoke an API key

To revoke an API key, click **Revoke** for the API key in the list you want to revoke. You can also revoke an API key by clicking **Revoke API Key** in the **API Key Details** sheet.

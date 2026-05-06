---
title: Manage accounts
headertitle: Manage accounts
linkTitle: Manage accounts
description: Create and manage multiple accounts for your organization.
headcontent: Create and manage multiple accounts for your organization
tags:
  feature: early-access
menu:
  stable_yugabyte-cloud:
    identifier: manage-accounts
    parent: managed-security
    weight: 50
type: docs
---

You can add multiple accounts for your organization. For example, you might create separate accounts for different products, or departments in your organization, or for different environments, such as development, staging, and production. Each account has its own access control.

{{<tip title="Early Access">}}
This feature is Early Access; to try it, contact {{% support-cloud %}}.
{{</tip>}}

Each account can have its own users, and can create and manage its own clusters. A single user can be added to multiple accounts, with different roles and permissions in each.

To manage accounts, click the Profile icon in the top right corner and choose **Manage Accounts** to display the **Manage Account** page.

Note that only accounts designated by Yugabyte Support when you enabled the feature can add and manage accounts.

![Accounts page](/images/yb-cloud/managed-accounts.png)

Your accounts are listed under **Organization**.

Keep in mind the following when using multiple accounts:

- When [adding users](../manage-access/), be sure to [switch to the account](#switch-accounts) where you want to add them first.
- To add users to multiple accounts, you must sign in to each account and send an invitation.
- If multiple accounts are enabled, all users must choose the account to sign into when they sign in.
- Each account uses a different [billing profile](../../cloud-admin/cloud-billing-profile/). You must set up a new billing profile for each account you create.

## Add accounts

By default, your organization has a single account.

To add an account:

1. Click the Profile icon in the top right corner and choose **Create Account**, or, on the **Manage Account** page, click **Add Account**.
1. Enter a name for the account and click **Create Account**.

The account is listed under **Accounts** on the **Manage Account** page.

## Switch accounts

To switch to an account:

1. On the **Manage Account>Accounts** tab, click **Switch Account** for the account you want to switch to.
1. Sign in to the account.

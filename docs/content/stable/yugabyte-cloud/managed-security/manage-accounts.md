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

## Overview

Using multi-account management, you can add multiple accounts for your organization, each with its own access control and billing.

{{<tip title="Early Access">}}
Multi-account management is Early Access; to try it, contact {{% support-cloud %}}.
{{</tip>}}

### Before multi-account management

Currently, when you sign up for YugabyteDB Aeon, you get a single account, and multi-account management is not enabled.

- All users you invite, all clusters you create, billing, VPCs, API keys, and access controls live inside that one account.
- Users can only belong to that one account, and the same user cannot sign in to access a second, separate Aeon account.
- If you require environment separation (for example, staging vs. production) or team isolation, you must create entirely separate Aeon sign-ups with different email addresses.

### What multi-account management adds

With Multi-account management, a single user can be a member of multiple accounts simultaneously under a single organization, with a different role in each account. Each account remains fully isolated:

- **Clusters** - each account has its own clusters; no cross-account visibility.
- **Billing** - each account has its own billing profile and usage tracking.
- **Users and roles** - each account controls its own user list and RBAC independently. Organization Super Admins are added to every account as an Admin.
- **VPCs, IP allow lists, API keys, and other resources** - all scoped to the account.

This is conceptually similar to how AWS Organizations or GCP projects work: a single identity can access multiple isolated environments, choosing which one to enter at sign-in.

A common use case is separating environments (development, staging, production) or separating business units or product lines that need distinct budgets, teams, and clusters, while still allowing designated administrators to move between accounts with the same login.

Multi-account is in Early Access; see the [Limitations](#limitations-early-access).

## Manage accounts

To manage your organization and its accounts, click the **Organization** menu in the top left corner and choose your organization to display the **Accounts** page.

![Accounts page](/images/yb-cloud/managed-accounts.png)

The **Accounts** page lists the accounts in your organization.

To rename an account, click the **Edit Account Name** icon next to the name in the list, enter a new name, and click **Confirm**.

To delete an account, click **...** and choose **Delete Account**. Note that you cannot delete the account where you are currently signed in; switch accounts and return to this page to delete the account.

Note that only users designated by Yugabyte Support when you enabled the feature can add and manage accounts.

Keep in mind the following when using multiple accounts:

- Account users are managed from the account. To [manage users](../manage-access/) for an account, first [switch to the account](#switch-accounts).
- To add users to multiple accounts, you must sign in to each account and send an invitation.
- If multiple accounts are enabled, all users must choose the account to sign into when they sign in.

To change your organization name, navigate to **Organization > Settings** and enter a new name.

### Add accounts

To add an account:

1. Click the **Organization** menu in the top left corner and choose your organization and, on the **Accounts** page, click **Create Account**.
1. Enter a name for the account and click **Create Account**.

The account is listed on the **Accounts** page.

### Switch accounts

To switch between accounts:

- Click the **Organization** menu in the top left corner and choose the account you want to switch to.

## Manage organization users

To view all users in your organization:

- Navigate to **Organization > Security > Access Control > Users**.

You can delete a user from your organization; this deletes the user from all accounts. To add users, go to the individual account and [invite them](../manage-access/).

Each user you add to individual accounts is added to your organization as a **Member** role.

To change a user's role, in the **Organization Role** column, choose a role.

- You cannot modify your own role.
- Your organization can have a maximum of 5 Super Admins.
- Users who are promoted to Super Admin are added to _all_ your accounts as an Admin.

## View organization billing

Each account has its own [billing profile](../../cloud-admin/cloud-billing-profile/). You must set up a new billing profile for each account you create.

To view a summary of billing for all accounts:

- Navigate to **Organization > Billing**.

The organization **Billing** page lists all your accounts and the total across all accounts in your organization for the current billing period.

You can view different billing periods or a custom range using the billing period dropdown menu.

To access the billing for an individual account, in the list, click **...** and choose **View Detailed Breakdown**. This takes you to the account's Billing page.

## Limitations

- **You must contact Yugabyte Support to enable it.** Contact {{% support-cloud %}} to enable multi-account for your organization. Once enabled, the users designated by Yugabyte Support (when enabling the feature) can create and manage accounts in Aeon without further support.
- **No automation.** Account management is UI-only, there is no API, Terraform provider, or CLI support for creating or deleting accounts. Automation of account lifecycle management is planned for GA.

---
title: Manage accounts
headertitle: Manage accounts
linkTitle: Manage accounts
description: Create and manage multiple accounts for your organization.
headcontent: Create and manage multiple accounts for your organization
menu:
  stable_yugabyte-cloud:
    identifier: manage-accounts
    parent: managed-security
    weight: 50
type: docs
---

Using multi-account management, you can add multiple accounts for your organization, each with its own access control and billing.

A single user can be a member of multiple accounts simultaneously under a single organization, with a different role in each account. Each account remains fully isolated:

- **Clusters** - each account has its own clusters; no cross-account visibility.
- **Billing** - each account has its own billing profile and usage tracking.
- **Users and roles** - each account controls its own user list and RBAC independently. Organization Super Admins are added to every account as an Admin.
- **VPCs, IP allow lists, API keys, and other resources** - all scoped to the account.

This is conceptually similar to how AWS Organizations or GCP projects work: a single identity can access multiple isolated environments, choosing which one to enter at sign-in.

A common use case is separating environments (development, staging, production) or separating business units or product lines that need distinct budgets, teams, and clusters, while still allowing designated administrators to move between accounts with the same login.

## Switch accounts

Switch between accounts using the **Account Switcher** menu in the top left corner of the Aeon window.

Users can switch between accounts they have joined. Organization Super Admins can switch between all accounts and access organization settings.

## Manage your organization

Only an organization Super Admin can access organization settings and manage accounts.

To access organization settings, in the **Account Switcher** menu, choose your organization.

### Manage accounts

Manage organization accounts on the organization **Accounts** page.

The **Accounts** page lists the accounts in your organization.

To rename an account, click the **Edit Account Name** icon next to the name in the list, enter a new name, and click **Confirm**.

Keep in mind the following when using multiple accounts:

- Account users and roles are managed from the account. To [manage users](../manage-access/) for an account, first [switch to the account](#switch-accounts).
- To add a user to multiple accounts, switch to each account and add them from the **Users** tab. Users who already belong to your organization are added immediately. New users receive an invitation and must accept it before they can be added to other accounts.
- When you sign in to YugabyteDB Aeon, you are signed in to the account you used most recently. Use the [Account Switcher](#switch-accounts) to change accounts.

To change your organization name, select the **Settings** page and enter a new name.

#### Add accounts

With the Enterprise plan, you can add additional accounts to your organization.

To add an account:

1. On the **Organization > Accounts** page, click **Create Account**.
1. Enter a name for the account and click **Create Account**.

The account is listed on the **Accounts** page.

#### Delete accounts

To delete an account:

- On the **Organization > Accounts** page, in the list, click **...** for the account and choose **Delete Account**.

You cannot delete accounts in the following circumstances:

- You are currently signed in to the account; [switch to another account](#switch-accounts) first.
- It is the only account.
- The account has VPCs or clusters.
- The account has charges or unpaid invoices.

### Manage organization users and authentication

To view all users in your organization:

1. In the **Account Switcher** menu, choose your organization.
1. On the **Security** page, select **Access Control > Users**.

You can delete a user from your organization; this deletes the user from your organization and all accounts.

To add users, go to the individual account and [invite them](../manage-access/).

Each user you add to individual accounts is added to your organization as a **Member** role. Members can switch between accounts they have joined.

To change a user's role, in the **Organization Role** column, choose a role.

If you are demoting a Super Admin to Member, you must also assign an account role for the user for the accounts in your organization.

- You cannot modify your own role.
- Your organization can have a maximum of 5 Super Admins.
- Users who are promoted to Super Admin are added to _all_ your accounts as an [Admin](../managed-roles/#built-in-account-roles).
- There must always be at least one Super Admin.
- You can't change the role of a user with a pending invitation.

#### Social logins and federated authentication

To configure social logins or federated authentication:

- On the **Security** page, select **Access Control > Authentication**.

For more information on configuring social logins or federated authentication, refer to [Manage account authentication](../managed-authentication/).

### View organization activity

To audit organization activity, navigate to **Organization > Security > Activity**.

The **Activity** tab provides the following two logs:

- **Console Activity** - lists organization activity, including the activity type, who performed the activity, timestamp, and result.

    Organization activity includes creating, editing, and deleting accounts; changing the organization name; changing organization user roles; removing users; changing login types and federated authentication.

- **Authentication Activity** - lists all login activity, including the client IP address, activity type, number of attempts, timestamp, and result.

To view activity details and associated messages, select the activity in the list to display the **Activity Details** sheet.

To filter the activity list, enter a search term. You can also filter the list by Source, Activity, Performed by, Result, and Date range.

For [cluster activity](../../cloud-monitor/monitor-activity/), navigate to the cluster **Activity** tab.

For individual [account activity](../../cloud-secure-clusters/cloud-activity/), [switch to the account](#switch-accounts) and navigate to **Security > Activity**.


### View organization billing

Each account has its own [billing profile](../../cloud-admin/cloud-billing-profile/). You must set up a new billing profile for each account you create.

To view a summary of billing for all accounts:

- Navigate to **Organization > Billing**.

The organization **Billing** page lists all your accounts and the total across all accounts in your organization for the current billing period.

You can view different billing periods or a custom range using the billing period dropdown menu.

To download a per-account summary for the currently selected period, click **Download Billing Summary**.

To access the billing for an individual account, in the list, click **...** and choose **View Detailed Breakdown**. This takes you to the account's Billing page.

## Limitation

Account management is UI-only; there is no API, Terraform provider, or CLI support for managing accounts.
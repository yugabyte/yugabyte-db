---
title: Manage accounts
headertitle: Manage accounts
linkTitle: Manage accounts
description: Create and manage multiple accounts for your organization.
headcontent: Create and manage multiple accounts for your organization
cascade:
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

### Before multi-account management

When you sign up for YugabyteDB Aeon, you get a single account. All users you invite, all clusters you create, billing, VPCs, API keys, and access controls live inside that one account. A user could only belong to one account — there was no way for the same login to access a second, separate account. Organizations that needed environment separation (for example, staging vs. production) or team isolation had to create entirely separate Aeon sign-ups with different email addresses.

### What multi-account management adds

Multi-account management decouples the *user* entity from the *account* entity. A single user can now be a member of multiple accounts simultaneously, with a different role in each. Each account remains fully isolated:

- **Clusters** - each account has its own clusters; no cross-account visibility.
- **Billing** - each account has its own billing profile and usage tracking.
- **Users and roles** - each account controls its own user list and RBAC independently.
- **VPCs, IP allow lists, API keys, and other resources** - all scoped to the account.

This is conceptually similar to how AWS Organizations or GCP projects work: a single identity can access multiple isolated environments, choosing which one to enter at sign-in.

A common use case is separating environments (development, staging, production) or separating business units or product lines that need distinct budgets, teams, and clusters - while still allowing designated administrators to move between accounts with the same login.

## Limitations (Early Access)

This feature is in Early Access. The following limitations apply to the current release; they are being addressed in the GA version.

- **Enablement requires Yugabyte Support.** Multi-account management is not self-service for initial setup. Contact {{% support-cloud %}} to have it enabled for your organization. Once enabled, a designated super admin can create and manage accounts through the UI without further engineering involvement.
- **Account creation is UI-only.** There is no API, Terraform provider, or CLI support for creating or deleting accounts. Automation of account lifecycle management is planned for GA.
- **Accounts cannot be deleted.** Once created, accounts cannot be removed. This restriction will be lifted in the GA release.
- **No aggregated user management.** You cannot view all users across all accounts in one place, or remove a user from all accounts at once. User management must be done account by account.
- **SSO requires a separate app per account.** If your organization uses federated authentication (SSO/IdP), you must configure a separate identity provider application for each account. This differs from the industry-standard experience (AWS, GCP) where one IdP application covers all accounts. Single IdP configuration at the organization level is planned for GA.
- **Account switching requires reauthentication.** Switching between accounts currently requires signing in again. Seamless account switching without reauthentication is planned for GA.
- **No organization-level overview.** There is no consolidated view of all accounts, their users, or cross-account activity. An organization management layer with aggregated views is planned for GA.

{{<tip title="Early Access">}}
This feature is Early Access; to try it, contact {{% support-cloud %}}.
{{</tip>}}

Each account can have its own users, and can create and manage its own clusters. A single user can be added to multiple accounts, with different roles and permissions in each.

To manage accounts, click the Profile icon in the top right corner and choose **Manage Accounts** to display the **Manage Accounts** page.

Note that only users designated by Yugabyte Support when you enabled the feature can add and manage accounts.

## Add accounts

By default, your organization has a single account.

To add an account:

1. Click the Profile icon in the top right corner and choose **Create Account**, or, on the **Manage Account** page, click **Add Account**.
1. Enter a name for the account and click **Create Account**.

The account is listed under **Accounts** on the **Manage Account** page.

## Switch accounts

To switch to an account:

1. On the **Manage Account > Accounts** tab, click **Switch Account** for the account you want to switch to.
1. Sign in to the account.

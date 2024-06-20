---
title: Billing and invoices
linkTitle: Manage billing
description: Manage your YugabyteDB Aeon billing profile and payment methods and view invoices.
headcontent: Manage your plan and payment, and review invoices and usage
menu:
  preview_yugabyte-cloud:
    identifier: cloud-billing-profile
    parent: cloud-admin
    weight: 200
type: docs
---

Review your usage and charges, manage your plan, and manage your billing information using the **Usage & Billing** page.

Yugabyte bills for its services as follows:

- Charges by the minute for your YugabyteDB Aeon clusters, at rates based on your plan.
- Tabulates costs daily.
- Displays your current monthly costs under **Invoices** on the **Usage & Billing** page.

**Invoices are sent to the email provided in the Billing Profile**.

For information on YugabyteDB Aeon plans and add-ons, refer to [YugabyteDB Pricing](https://www.yugabyte.com/pricing/). For a description of how cluster configurations are costed, refer to [Cluster costs](../cloud-billing-costs/).

![Usage & Billing tab](/images/yb-cloud/cloud-admin-billing.png)

The **Usage & Billing** page has the following tabs: **Usage**, **Plan**, **Invoices**, and **Billing Information**.

## Usage

Use this section to review your usage by cluster over time. You can view cumulative and daily usage of the following:

- cluster compute (vCPU hours)
- disk storage
- cloud backup storage
- data transfer

## Plan

Use this section to subscribe to a plan, or manage and review your plan details, including any add-ons.

To make changes to your plan, click **Manage Plan**.

## Invoices

Your invoice is generated at the end of each month. The amount is charged automatically to the default credit card, and the invoice and receipt are emailed to the address on the billing profile. If you are using a payment method other than credit card, the invoice will be settled out of band.

**Invoices** lists your active and past invoices, along with a running total of usage for the current billing period. To review a specific invoice, select it in **Invoice History**.

### Active Invoice

Shows the billing details for the current billing cycle, including:

- Running total - running total of the amount you owe in the current billing period; Yugabyte updates this once a day.
- Billing option - your billing plan (pay-as-you-go or subscription).
- Billing period - the start and end date of the current billing period. Your first billing period starts the day you created your billing profile and ends on the last day of the month; subsequent periods start on the first day of the month and end on the last.
- Previous invoice - the amount of the previous invoice.
- Last billed on - the date of the previous invoice.
- This time last month - the amount owing on the same day in the previous billing period (for example, if the current day is the 13th, this displays the amount that was owed on the 13th of last month).

Click **Invoice Details** to view the invoice [Summary](#invoice-summary) for the active invoice.

### Usage Breakdown

Shows a breakdown of costs for the current billing cycle. YugabyteDB Aeon billing is based on your actual usage across the following dimensions:

- Instance Capacity
- Disk Storage
- Backup Storage
- Data Transfer
- Disk IOPS

### Invoice History

Lists your invoices with following details:

- Status - one of the following:
  - RUNNING - the invoice is for the current billing period.
  - INVOICED - the invoice has been finalized by Yugabyte and has been emailed to the billing email address on record.
  - PENDING - includes charges for the current subscription cycle; you should never have more than one invoice in this state.
  - DISPUTE - the invoice is disputed and not yet been resolved.
  - FORGIVEN - 100% of the invoice charge has been forgiven and, if the charge succeeded, has been refunded by Yugabyte.
- Invoice number - the invoice serial number.
- Invoice Date - date the invoice was generated.
- Invoice Period - billing period for the invoice.
- Subtotal - total invoiced amount to be paid.

Select an invoice in the list to view the invoice **Summary**, which includes a detailed breakdown of costs and usage details. These details are also included on the invoice that is emailed to the billing address.

To download the invoice as a PDF, click the **Download** icon.

### Invoice summary

To view invoice details, select an invoice in **Invoice History** to display its **Summary**.

To download the invoice as a PDF, click **Download Invoice**.

**Summary** shows the following invoice details:

- Total - the amount you owe in the billing period (or running total in the current billing period; Yugabyte updates the running total once a day).
- Billing option - your billing plan (pay-as-you-go or subscription).
- Billing period - the start and end date of the current billing period. Your first billing period starts the day you created your billing profile and ends on the last day of the month; subsequent periods start on the first day of the month and end on the last.
- Previous invoice - the amount of the previous invoice.
- Last billed on - the date of the previous invoice.
- This time last month - the amount owing on the same day in the previous billing period (for example, if the current day is the 13th, this displays the amount that was owed on the 13th of last month.
- Payment status - whether the invoice was paid.
- Payment date - the date the invoice was paid.
- Payment method - the payment method used to pay the invoice.

**Summary by Cluster** shows a breakdown of costs for each cluster.

**Summary by Infrastructure** shows a breakdown of costs by usage type.

**Summary by Usage** shows detailed costs for each cluster. Click a cluster to view usage details for each dimension, including:

- The instance minutes, price, and amount.
- Data storage usage in GB-hours, price, and amount.
- Backup storage usage in GB-hours, price, and amount.
- Data transfer usage in GB, price, and amount.

For information on how your invoice is costed, refer to [Cluster costs](../cloud-billing-costs/).

## Billing Information

Use this section to manage your contact information and payment method. You can pay using a credit card, or other payment methods such as debit, ACH, and invoicing.

Credit cards are self service. For other payment methods, create your billing profile and set the **Billing Options** to **Other**; after you create your profile, contact {{% support-cloud %}} to set up payment.

If you want to switch from paying by credit card to another method, contact {{% support-cloud %}}.

### Add or edit your Billing Profile

To add or change your billing profile:

1. On the **Usage & Billing** page, select **Profile and Payment Methods** and click **Edit Billing Profile** to display the **Edit Billing Profile** sheet. If you haven't yet created a billing profile, click **Create Billing Profile**.
1. Edit your contact information.
1. If you are creating your billing profile, enter your credit card details. For other payment methods, set the **Billing Options** to **Other**. You can only set the **Billing Options** if you are creating your profile.
1. Click **Save**.

### Manage credit cards

To add a credit card:

1. On the **Usage & Billing** page, select **Profile and Payment Methods** and click **Add Card** to display the **Add Credit Card** dialog.
1. Enter your credit card details.
1. To use the card as the default for payment, choose **Set as default credit card**.
1. Click **Save**.

To delete a card, click the **Delete** icon, then click **Confirm**. You cannot delete the default card.

To change the default for payment, in the Card list, select the **Default Card** option for the card.

### View credits and discounts

The **Credits and Discounts** section displays any credits applied to your account, including the end date, days left, amount used, and amount remaining. Credits are automatically applied to the next invoice.

---
title: Manage your billing profile and payment method
linkTitle: Billing
description: Manage your Yugabyte Cloud billing profile and payment methods and view invoices.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  latest:
    identifier: cloud-billing-profile
    parent: cloud-admin
    weight: 200
isTocNested: true
showAsideToc: true
---

Review your cloud charges and manage your billing profile and payment methods using the **Billing** tab.

Only the cloud account Admin user (the user who created the Yugabyte Cloud account) can access the billing and payment details.

Yugabyte bills for its services as follows:

- Charges by the minute for your Yugabyte Cloud clusters.
- Tabulates costs daily.
- Displays your current monthly costs under **Invoices** on the **Billing** tab.

Yugabyte Cloud Billing is based on your actual usage across the following dimensions:

- Instance Capacity
- Disk Storage
- Backup Storage
- Data Transfer
 
**Invoices are sent to the email provided in the billing profile**. Refer to [Cluster costs](../cloud-billing-costs) for a summary of how cluster configurations are costed.

![Admin Billing tab](/images/yb-cloud/cloud-admin-billing.png)

The **Billing** tab has the following sections: **Profile and Payment Methods**, and **Invoices**.

## Profile and Payment Methods

Use this section to manage your contact information and payment method. You can pay using a credit card, or other payment methods such as debit, ACH, and invoicing.

Credit cards are self service. For other payment methods, create your billing profile and set the **Billing Options** to **Other**; once you have created your profile, contact [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431) to set up payment.

If you want to switch from paying by credit card to another method, contact [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431).

### Add or edit your Billing Profile

To add or change your billing profile:

1. On the **Billing** tab, select **Profile and Payment Methods** and click **Edit Billing Profile** to display the **Edit Billing Profile** sheet. If you haven't yet created a billing profile, click **Create Billing Profile**. 
1. Edit your contact information.
1. If you are creating your billing profile, enter your credit card details. For other payment methods, set the **Billing Options** to **Other**. You can only set the **Billing Options** if you are creating your profile.
1. Click **Save**.

### Manage credit cards

To add a credit card:

1. On the **Billing** tab, select **Profile and Payment Methods** and click **Add Card** to display the **Add Credit Card** dialog. 
1. Enter your credit card details.
1. To use the card as the default for payment, choose **Set as default credit card**.
1. Click **Save**.

To delete a card, click the **Delete** icon, then click **Confirm**. You cannot delete the default card.

### View credits

The **Credits** section displays any credits applied to your account, including the expiration date, name, amount used, and amount remaining. Credits are automatically applied to the next invoice.

## Invoices

This section lists your current and past invoices, along with a summary of cloud usage.

Select an invoice in the list to view a detailed breakdown of costs and usage details.

### Active Invoice

Shows the billing details for the current billing cycle, including:

- Running Total - running total of the amount you owe in the current billing period; Yugabyte updates this once a day.
- Billing option - your billing plan.
- Billing period - the start and end date of the current billing period. Your first billing period starts the day you created your billing profile and ends on the last day of the month; subsequent periods start on the first day of the month and end on the last.
- Previous invoice amount - the previous invoice paid amount.
- Last billed on - the date of the previous invoice.
- This time last month: the amount owing on the same day in the previous billing period (for example, if the current day is the 13th, this displays the amount that was owed on the 13th of last month.

### Invoice History

Shows the invoice history with following details:

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

To download the invoice as a PDF, click **Download PDF**.

At the end of each month, your invoice is generated, the amount is charged automatically to the default credit card, and the invoice and receipt are emailed automatically to the address on the billing profile.

If you are using a payment method other than credit card, the invoice will be settled out of band.

### Usage summary

<!--
## Cost Estimator

Use the cost estimator to see estimated monthly and yearly pricing for for your cloud, based on configuration, backup, and data transfer settings.
-->

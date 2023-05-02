---
title: Create and configure alerts
headerTitle: Create and configure alerts
linkTitle: Configure alerts
description: Configure alerts and health check
menu:
  preview_yugabyte-platform:
    identifier: set-up-alerts-health-checking
    parent: configure-yugabyte-platform
    weight: 40
type: docs
---

YugabyteDB Anywhere can check universes for issues that may affect deployment. Should problems arise, YugabyteDB Anywhere can automatically issue alert notifications.

For additional information, see the following:

- [Alerts](../../alerts-monitoring/alert/)
- [Metrics](../../troubleshoot/universe-issues/#use-metrics/)
- [Alerts and Notifications in YugabyteDB Anywhere](https://www.yugabyte.com/blog/yugabytedb-2-8-alerts-and-notifications/)

You can use preconfigured alerts provided by YugabyteDB Anywhere, or create and configure your own alerts based on the metrics' conditions.

You can access YugabyteDB Anywhere health monitor and configure alerts by navigating to **Admin > Alert Configurations**, as per the following illustration:

![Configure alerts](/images/yp/config-alerts1.png)

The **Alert Configurations** view allows you to perform the following for specific universes or for your instance of YugabyteDB Anywhere:

- Create new alert policies.
- Modify, delete, activate, or deactivate existing alerts, as well as send test alerts via **Actions**.
- Find alerts by applying filters.
- Define maintenance windows during which alerts are not issued.

## Create alerts

Regardless of the alert level, you create an alert as follows:

1. Navigate to **Alert Configurations > Alert Policies**.

1. Click either **Create Alert Policy > Universe Alert** or **Create Alert Policy > Platform Alert**, depending on the scope of the alert. Note that the scope of **Platform Alert** is YugabyteDB Anywhere.

1. Select a policy template to use, and then configure settings by completing the fields whose default values depend on the template:

    Templates are available for alerts related to YugabyteDB Anywhere operations, YugabyteDB operations, as well as YSQL and YCQL performance. For more information on available templates, refer to [Alert policy templates](../alert-policy-templates/).

    Most of the template fields are self-explanatory. The following fields are of note:

    - The **Active** option allows you to define the alert as initially active or inactive.

    - The **Threshold** field allows you to define the value (for example, number of milliseconds, resets, errors, nodes) that must be reached by the metric to trigger the alert.

    - The **Alert Destination** field allows you to select one of the previously defined recipients of the alert. For more information, see [Define alert destinations](#define-alert-destinations).

1. Click **Save**.

## Define alert destinations

When an alert is triggered, alert data is sent to a specific alert destination that consists of one or more [notification channels](#define-notification-channels). You can define a new destination for your alerts, view details of an existing destination, edit, or delete an existing destination as follows:

1. Navigate to **Alert Configurations > Alert Destinations**.

1. To add a new alert destination, click **Add Destination** and then complete the form shown in the following illustration:

    ![Add destination](/images/yp/config-alerts4.png)

    The preceding form allows you to either select an existing notification channel or create a new one by clicking **Add Channel** and completing the **Create new alert channel** dialog, as described in [Define notification channels](#define-notification-channels).

1. Click **Save**.

To view details, modify, or delete an existing destination, click **Actions** corresponding to the destination and then select either **Channel Details**, **Edit Destination**, or **Delete Destination**.

## Define notification channels

In YugabyteDB Anywhere, a notification channel defines how an alert is issued (via an email, a Slack message, a webhook message, or a PagerDuty message) and who should receive it. You can create a new channel, as well as modify or delete an existing ones.

You can also customize the templates used to create email and webhook notifications.

To manage notification channels, navigate to **Alert Configurations > Notification Channels**.

### Create, edit, and delete notification channels

To create a new channel, do the following:

1. Click **Add Channel** and then complete the **Create new alert channel** dialog.

1. Select the target for the channel.

    If you select **Email** as the target, perform the following:

      - Provide a descriptive name for your channel.

      - To use the details provided on the **Health** tab for the recipients and the mail server, select the **Use recipients specified on Health tab** and **Use SMTP server specified on Health tab** options. See [Configure health check](#configure-health-check).

        Otherwise, use the **Emails** field to enter one or more valid email addresses separated by commas, and enter the Simple Mail Transfer Protocol (SMTP) settings to use.

    If you select **Slack** as a notification delivery method, perform the following:

      - Provide a descriptive name for your channel.

      - Use the **Slack Webhook URL** field to enter a valid URL.

    If you select **PagerDuty** as a notification delivery method, perform the following:

      - Provide a descriptive name for your channel.

      - Enter a PagerDuty API key and service integration key.

    If you select **WebHook** as a notification delivery method, perform the following:

      - Provide a descriptive name for your channel.

      - Use the **Webhook URL** field to enter a valid URL.

1. Click **Create**.

To modify an existing channel, click its corresponding **Actions > Edit Channel** and then complete the **Edit alert channel** dialog that has the same fields as the **Create new alert channel** dialog.

To delete a channel, click **Actions > Delete Channel**.

### Customize notification templates

Email and WebHook notifications use a template that you can customize. All alert policies that generate email or webhook alerts use these notification templates.

To customize the notification templates, do the following:

1. On the **Notification Channels** tab, click **Customize Notification Templates**.

1. Select the template to customize by choosing **Email Template** or **Webhook Template**.

1. Edit the template.

    For the Email Template, you can edit the email subject and body text.

    To insert variables into the template, click **Insert Variable** and choose the variable to add. You can choose from the built-in system variables, or custom variables that you can create. For example, using a custom variable, you could define team names to be used in alerts.

    To create a custom variable, click **Create Custom Variable**, enter a name for the variable, and define the set of possible values for the variable. Set one of the values to be the default. Click **Save** to create the variable.

1. Click **Save** when you are done.

## Configure health check

You can define parameters and fine-tune the health check that YugabyteDB Anywhere performs on its universes, as follows:

1. Navigate to **Alert Configurations > Health** to open the **Alerting controls** view.

1. Set the **Callhome Level**.

1. Use the **Health check interval** field to set how often to perform the health check (in minutes).

1. Use the **Active alert notification interval** field to define the notification period (in milliseconds) for resending notifications for active alerts. The default value of 0 means that only one notification is issued for an active alert.

1. To send email notifications for health checks, turn on the **Email notifications** option and enter a comma-separated list of email addresses to which alerts are to be sent in the **Alert emails** field.

    - Use the **Send alert emails to Yugabyte team** option to enable sending the same alerts to Yugabyte Support.

    - Enter the email address to use for sending the outgoing emails in the **Email From** field.

    - Enter the details of the SMTP server to use to send the email notifications, including the server address, port number, the user credentials, and desired security settings.

1. Use the **Health Check email report interval** field to set how often to send health check email (in minutes).

1. To only include errors in health check emails, select the **Only include errors in alert emails** option.

1. Click **Save**.

## Configure maintenance windows

You can configure maintenance windows during which alerts are snoozed by navigating to **Alert Configurations > Maintenance Windows**.

To show previous completed maintenance windows, select the **Show Completed Maintenance** option.

To extend an active maintenance window, click **Extend** and select the amount of time.

To mark the maintenance as completed, modify its parameters, or delete it, by click **Actions** and select one of the options.

To create a maintenance window, do the following:

1. On the **Maintenance Windows** tab, click **Add Maintenance Window**.

1. Enter a name and description for the window.

1. Set a start and end time.

1. Select a target for the window.

1. Click **Save**.

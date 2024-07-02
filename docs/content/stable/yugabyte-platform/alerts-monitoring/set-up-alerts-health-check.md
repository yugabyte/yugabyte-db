---
title: Create and configure alerts
headerTitle: Create and configure alerts
linkTitle: Configure alerts
description: Configure alerts and health check
menu:
  stable_yugabyte-platform:
    identifier: set-up-alerts-health-check
    parent: alert
    weight: 15
type: docs
---

YugabyteDB Anywhere has default, preconfigured alerts, both at the YugabyteDB Anywhere and universe level. Universe alerts can be configured globally for all universes, or per specific universe. In addition to the default alerts, you can configure your own alerts based on a specific condition on any available alert policy template.

To review alerts that have triggered, refer to [Alerts](../../alerts-monitoring/alert/). To view performance metrics, refer to [Performance metrics](../../alerts-monitoring/anywhere-metrics/).

You can configure alerts by navigating to **Admin > Alert Configurations**.

The **Alert Configurations** view allows you to perform the following for specific universes or for your instance of YugabyteDB Anywhere:

- Create new alert policies.
- Modify, delete, activate, or deactivate existing alerts, as well as send test alerts via **Actions**.
- Find alerts by applying filters.
- Configure the health check interval.
- Define maintenance windows during which alerts are not issued.

## Manage alert policies

Alerts are defined by an alert policy, which consists of the following:

- A [policy template](../alert-policy-templates/). The template defines the metric and preset values for the conditions that trigger the alert. Depending on the metric, you can configure duration, severity levels, operators, and the threshold.
- An [alert destination](#manage-alert-destinations). Whenever an alert triggers, it sends an alert data to its designated alert destinations.
- Alert destinations consist in turn of one or more [notification channels](#manage-notification-channels). A notification channel defines the means by which an alert is sent (for example, Email or Slack) as well as who should receive the notification.

### Create alerts

Regardless of the alert level, you create an alert as follows:

1. Navigate to **Admin > Alert Configurations > Alert Policies**.

1. Click either **Create Alert Policy > Universe Alert** or **Create Alert Policy > Platform Alert**, depending on the scope of the alert. Note that the scope of **Platform Alert** is YugabyteDB Anywhere.

1. Select a policy template to use, and then configure settings by completing the fields whose default values depend on the template.

    Templates are available for alerts related to YugabyteDB Anywhere operations, YugabyteDB operations, as well as YSQL and YCQL performance. For more information on available templates, refer to [Alert policy templates](../alert-policy-templates/).

    Most of the template fields are self-explanatory. The following fields are of note:

    - The **Active** option allows you to define the alert as initially active or inactive.

    - The **Threshold** field allows you to define the value (for example, number of milliseconds, resets, errors, nodes) that must be reached by the metric to trigger the alert.

    - The **Alert Destination** field allows you to select one of the previously defined recipients of the alert. For more information, see [Manage alert destinations](#manage-alert-destinations).

    - If you selected an alert destination that uses a notification channel with one or more [custom variables](#customize-notification-templates) defined, click **Custom variables used by the notification channels** and choose the value to use in the alert for each custom variable.

1. Click **Save**.

## Manage alert destinations

When an alert is triggered, alert data is sent to a specific alert destination that consists of one or more [notification channels](#manage-notification-channels). You can define a new destination for your alerts, and view details of, edit, or delete an existing destination.

To manage notification channels, navigate to **Admin > Alert Configurations > Alert Destinations**.

To view details, modify, or delete an existing destination, click **Actions** corresponding to the destination and then choose either **Channel Details**, **Edit Destination**, or **Delete Destination**.

To create a new destination, do the following:

1. Click **Add Destination**.

1. Enter a name for the destination.

1. In the Choose Channels field, click to select notification channels for the alert destination.

    To create a new notification channel, click **Add Channel** and complete the **Create new alert channel** dialog, as described in [Manage notification channels](#manage-notification-channels).

1. Click **Save** to create the alert destination.

## Manage notification channels

In YugabyteDB Anywhere, a notification channel defines how an alert is issued (via an email, a Slack message, a webhook message, or a PagerDuty message) and who should receive it. You can create a new channel, as well as modify or delete existing ones.

You can also customize the templates used to create email and webhook notifications.

To manage notification channels, navigate to **Alert Configurations > Notification Channels**.

### Create, edit, and delete notification channels

To create a new channel, do the following:

1. Click **Add Channel** and then complete the **Create new alert channel** dialog.

1. Use the **Name** field to provide a descriptive name for your channel.

1. Select the target for the channel.

    If you select **Email** as the target, perform the following:

      - To use the details provided on the **Health** tab for the recipients and the mail server, select the **Use recipients specified on Health tab** and **Use SMTP server specified on Health tab** options. See [Configure health check](#configure-health-check).

        Otherwise, use the **Emails** field to enter one or more valid email addresses separated by commas, and enter the Simple Mail Transfer Protocol (SMTP) settings to use.

    If you select **Slack** as a notification delivery method, perform the following:

      - Use the **Slack Webhook URL** field to enter a valid URL.

    If you select **PagerDuty** as a notification delivery method, perform the following:

      - Enter a PagerDuty API key and service integration key.

    If you select **WebHook** as a notification delivery method, perform the following:

      - Use the **Webhook URL** field to enter a valid URL.

      - Chose the authentication type - Basic or Token.

      - For Basic, enter the username and password. For Token, enter the token header and value.

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

YugabyteDB Anywhere performs periodic checks on the universes under management.

You can define parameters and fine-tune the health check that YugabyteDB Anywhere performs, as follows:

1. Navigate to **Admin > Alert Configurations > Health** to open the **Alerting controls** view.

1. Set the **Callhome Level**.

1. Use the **Health check interval** field to set how often to perform the health check (in minutes).

1. Use the **Active alert notification interval** field to define the notification period (in milliseconds) for resending notifications for active alerts. The default value of 0 means that only one notification is issued for an active alert.

1. To send email notifications for health checks, turn on the **Email notifications** option and enter a comma-separated list of email addresses to which alerts are to be sent in the **Alert emails** field.

    - Use the **Send alert emails to Yugabyte team** option to enable sending the same alerts to Yugabyte Support.

    - Enter the email address to use for sending the outgoing emails in the **Email From** field.

    - Enter the details of the SMTP server to use to send the email notifications, including the server address, port number, the user credentials, and desired security settings.

    - Use the **Health Check email report interval** field to set how often to send health check email (in minutes).

    - To only include errors in health check emails, select the **Only include errors in alert emails** option.

1. Click **Save**.

## Configure maintenance windows

You can configure maintenance windows during which alerts are snoozed by navigating to **Admin > Alert Configurations > Maintenance Windows**.

To show previous completed maintenance windows, select the **Show Completed Maintenance** option.

To extend an active maintenance window, click **Extend** and select the amount of time.

To mark the maintenance as completed, modify its parameters, or delete it, click **Actions** and select one of the options.

To create a maintenance window, do the following:

1. On the **Maintenance Windows** tab, click **Add Maintenance Window**.

1. Enter a name and description for the window.

1. Set a start and end time.

1. Select a target for the window.

1. Click **Save**.

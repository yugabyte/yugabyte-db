---
title: Create and configure alerts
headerTitle: Create and configure alerts
linkTitle: Configure alerts
description: Configure alerts and health check
menu:
  latest:
    identifier: set-up-alerts-health-checking
    parent: configure-yugabyte-platform
    weight: 40
isTocNested: true
showAsideToc: true
---

Yugabyte Platform can check universes for issues that may affect deployment. Should problems arise, Yugabyte Platform can automatically issue alert notifications

For additional information, see the following: 

- [Alerts](../../alerts-monitoring/alert/)
- [Metrics](../../troubleshoot/universe-issues/#use-metrics/)
- [Alerts and Notifications in Yugabyte Platform](https://blog.yugabyte.com/yugabytedb-2-8-alerts-and-notifications/)

You can use preconfigured alerts provided by Yugabyte Platform, or create and configure your own alerts based on the metrics' conditions.

You can access Yugabyte Platform health monitor and configure alerts by navigating to **Admin > Alert Configurations**, as per the following illustration:

![Configure alerts](/images/yp/config-alerts1.png)

The **Alert Creation** view allows you to perform the following for specific universes or for your instance of Yugabyte Platform: 

- Create new alerts.
- Modify, delete, activate, or deactivate existing alerts, as well as send test alerts via **Actions**.

## Create alerts

Regardless of the alert level, you create an alert as follows: 

- Navigate to **Alert Configurations > Alert Creation**.

- Click either **Create Alert Config > Universe Alert** or **Create Alert Config > Platform Alert**, depending on the scope of the alert. 

- Select a template to use, and then configure settings by completing the fields whose default values depend on the template, as per the following illustration: <br><br>

  ![Create alert](/images/yp/config-alerts2.png)

  <br><br>

  Templates are available for alerts related to Yugabyte Platform operations, YugabyteDB operations, as well as YSQL and YCQL performance.<br>

  Most of the template fields are self-explanatory. The following fields are of note:

  - The **Active** field allows you to define the alert as initially active or inactive.<br>

  - The **Threshold** field allows you to define the value (for example, number of milliseconds, resets, errors, nodes) that must be reached by the metric in order to trigger the alert.<br>

  - The **Destination** field allows you to select one of the previously defined recipients of the alert. For more information, see [Define alert destinations](#define-alert-destinations).

- Click **Save**.

## Define notification channels

In Yugabyte Platform, a notification channel defines how an alert is issued (via an email, a Slack message, a webhook message, or a PagerDuty message) and who should receive it.<br>You can create a new channel, as well as modify or delete an existing one as follows: 

- Navigate to **Alert Configurations > Notification Channels**, as per the following illustration: 

  <br><br>

  ![Notification channel](/images/yp/config-alerts7.png)

  <br><br>

- To create a new channel, click **Add Channel** and then complete the **Create new alert channel** dialog shown in the following illustration:<br>

  ![New channel](/images/yp/config-alerts6.png)

  <br><br>

  If you select **Email** as a notification delivery method, perform the following: 

  - Provide a descriptive name for your channel.

  - Use the **Emails** field to enter one or more valid email addresses separated by commas. 

  - If you choose to configure the Simple Mail Transfer Protocol (SMTP) settings, toggle the **Custom SMTP Configuration** field and then complete the required fields.

  If you select **Slack** as a notification delivery method, perform the following: 

  - Provide a descriptive name for your channel.

  - Use the **Slack Webhook URL** field to enter a valid URL. 

  If you select **PagerDuty** as a notification delivery method, perform the following: 

  - Provide a descriptive name for your channel.

  - Enter a PagerDuty API key and service integration key. 

  If you select **WebHook** as a notification delivery method, perform the following: 

  - Provide a descriptive name for your channel.

  - Use the **Webhook URL** field to enter a valid URL. 

- To modify an existing channel, click its corresponding **Actions > Edit Channel** and then complete the **Edit alert channel** dialog that has the same fields as the **Create new alert channel** dialog.  
- To delete a channel, click **Actions > Delete Channel**.

## Define alert destinations

When an alert is triggered, alert data is sent to a specific alert destination that consists of one or more channels. You can define a new destination for your alerts, view details of an existing destination, edit or delete an existing destination as follows:

- Navigate to **Alert Configurations > Alert Destinations**, as per the following illustration: <br><br>

  ![Destinations](/images/yp/config-alerts3.png)<br><br>
- To add a new alert destination, click **Add Destination** and then complete the form shown in the following illustration:<br><br>

  ![Add destination](/images/yp/config-alerts4.png)

  <br><br>The preceding form allows you to either select an existing notification channel or create a new one by clicking **Add Channel** and completing the **Create new alert channel** dialog, as described in [Define notification channels](#define-notification-channels).

- Click **Save**.

- To view details, modify, or delete an existing destination, click **Actions** corresponding to this destination and then select either **Channel Details**, **Edit Destination**, or **Delete Destination**.

## Configure heath check

You can define parameters and fine-tune health check that Yugabyte Platform performs on its universes, as follows:

- Navigate to **Alert Configurations > Health** to open the **Alerting controls** view shown in the following illustration:<br><br>

  ![Health](/images/yp/config-alerts5.png)<br><br>

- Use the **Alert emails** field to define a comma-separated list of email addresses to which alerts are to be sent.
- Use the **Send alert email to Yugabyte team** field to enable sending the same alerts to Yugabyte Support.

- Complete the remaining fields or accept the default settings.

- If you enable **Custom SMTP Configuration**, you need to provide the address for the Simple Mail Transfer Protocol (SMTP) server, the port number, the email, the user credentials, and select the desired security settings.

- Click **Save**.


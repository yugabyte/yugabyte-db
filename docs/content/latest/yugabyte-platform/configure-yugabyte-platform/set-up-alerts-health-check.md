---
title: Configure alerts
headerTitle: Configure alerts
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

Yugabyte Platform can check universes for issues that may affect deployment. Should problems arise, Yugabyte Platform can automatically send email alerts.

You can access Yugabyte Platform health monitor and configure alerts by navigating to **Admin > Alert Configurations**, as per the following illustration:

![Configure alerts](/images/yp/config-alerts1.png)

The **Alert Creation** view allows you to create new alerts for specific universes or for your instance of Yugabyte Platform, as well as configure or delete existing alerts.

Regardless of the alert level, you create and configure an alert as follows: 

- Click either **Create Alert Config > Universe Alert** or **Create Alert Config > Platform Alert**.  

- Select a template to use, and then configure settings by completing the fields whose default values depend on the template, as per the following illustration: <br><br>

  ![Create alert](/images/yp/config-alerts2.png)

  <br><br>The **Destination** field allows you to select one of the previously defined recipients of the alert. 

- Click **Save**.

- Click **Alert Destinations** to define a new destination, view details of an existing destination, edit or delete a destination, as per the following illustration: <br><br>

  ![Destinations](/images/yp/config-alerts3.png)
  - To add a new destination, click **Add Destination** and then complete the form shown in the following illustration:<br><br>

    ![Add destination](/images/yp/config-alerts4.png)

  - Click **Save**.

  - To view, modify, or delete an existing destination, click **Actions** corresponding to this destination and then select either **Details**, **Edit Destination**, or **Delete Destination**.

- Click **Health** to define and fine tune the health check that Yugabyte Platform performs on the universes, as per the following illustration:<br><br>

  ![Health](/images/yp/config-alerts5.png)
  - Use the **Alert emails** field to define a comma-separated list of email addresses to which alerts are to be sent.
  - Use the **Send alert email to Yugabyte team** field to enable sending the same alerts to Yugabyte Support.
  - Complete the remaining fields or accept the default settings.
  - If you enable **Custom SMTP Configuration**, you need to provide the address for the Simple Mail Transfer Protocol server, the port number, the email and user credentials, and the desired security settings.
  - Click **Save**.


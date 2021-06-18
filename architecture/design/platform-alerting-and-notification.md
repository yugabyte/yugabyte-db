# Platform Alerting and Notification

Tracking GitHub Issue: [8963](https://github.com/yugabyte/yugabyte-db/issues/8963)


# Motivation
* Real time database alerts based on a user alert policy: Users can set alert policies based on their cluster performance metrics. Alert policies notify you when a performance metric rises above or falls below a threshold you set. 
* OOTB intelligent database health checks and default alerts: YB Platform will provide intelligent ootb health checks and alerts, when something goes wrong, but also when it thinks something may go wrong in the future, allowing you to stay ahead of issues that may arise. 
* Forward notifications to 3rd party centralized notification systems: Alert notifications can integrate with a customer's choice of centralized notification system so they can get a 360 view of their entire application stack. To start with we will allow forwarding notifications to SMTP destinations and then integrate with other systems - Slack, PagerDuty and Webhooks
* Build your own alerting - Allow forwarding and scraping metrics from Prometheus
* Allow interacting with alerting stack programmatically via APIs	
* While YB Platform will provide advanced alerting and notifications via UI, customers can also interact with the stack via APIs to make sure their Ops teams (with minimum knowledge of YB Platform) are able to turn on/off alerts during maintenance windows. 


# Usage

## Alert definition and types
* Yugabyte Platform will have default, preconfigured alerts, both at platform and cluster level. Cluster alerts can be configured globally for all clusters, or per specific cluster. In addition to the above default alerts, users can configure their alerts based on a specific condition on any available metric. 
![Platform alert configurations](https://github.com/ymahajan/yugabyte-db/blob/current-roadmap-updates/architecture/design/images/platform-alert-configurations.png)

* Every alert has the following information:
    * Alert name and description
    * Metric name
    * Target (platform vs specific cluster vs all clusters)
    * Metric threshold value
    * Operator (less than, equal to or greater than)
    * Duration
    * Severity (warning and severe)
    * Destination (email, slack, pagerduty, etc.)
    
![Platform create cluster alert](https://github.com/ymahajan/yugabyte-db/blob/current-roadmap-updates/architecture/design/images/platform-create-cluster-alert.png) 

* Duration configured as M minutes means that it is a time to wait for alert condition to be true for M more minutes after evaluation first succeeds before raising alerts.
* The check interval should be 1 minute for prometheus based alerts (the current default for the health check interval minute). The check interval is the amount of time from the start of one probe to the start of the next probe.

* Alert notifications should be sent in real time (rather than grouping all alerts into batches over X minutes before notifying on subscribe channel like Email)
* Alerts should be snoozed when universe/node creation or removal is in progress to avoid unnecessary alerts to be generated.
* When an universe is deleted, corresponding alerts should also be deleted.
* Should have the ability to send test alerts to ensure right alerts are raised for the defined condition and threshold.
* To resolve each alert playbook should be provided. For now. playbook should be just documentation with alert resolution information like - 
  * Explanation of the alert
  * Logs are available in the following directory - (path to logs)
  * Restart by running the following commands on the cluster’s Master/TServer node
  * If there is an OOM exception, increase the heap size and restart it. 
  * If this alert continues to appear, restart a specific component or inform #yb-escalation or YB support channel etc.

## Alert destinations

![Platform alert destinations](https://github.com/ymahajan/yugabyte-db/blob/current-roadmap-updates/architecture/design/images/platform-alert-destinations.png)

## View Alerts
To see a list of alerts, click the Alerts tab on the left. By default, alerts are sorted in reverse chronological order by the alert raised time, but should have the ability to reorder the list by clicking the column headings. 

![Platform alert list](https://github.com/ymahajan/yugabyte-db/blob/current-roadmap-updates/architecture/design/images/platform-alert-list.png)


* “Triggered” means that on the most recent alert check, when the configure threshold is breached. For example If your alert checks whether CPU is above 80%, your alert should be triggered as long as CPU is above 80%.
* “Ok” means that the most recent alert check indicates that the configured threshold was not breached. This doesn’t mean that the Alert was not triggered previously. If your CPU value is now 40% your alert will show as Ok.
## Alert actions
* **Suspend alerts during maintenance window**
* Should have the ability to temporarily suspend alerts on a resource by creating an alert maintenance window. For example, you can create a maintenance window that suspends host alerts while you shut down hosts for maintenance.
* Should have the ability to Add or Edit or Delete a maintenance window
* Select the target components for which to suspend alerts.
* Select the time period for which to suspend alerts.
* **Acknowledge alerts to avoid repetitive alerts**
When you acknowledge the alert, Platform should send no further notifications to the alert’s distribution list until the acknowledgement period has passed or until you resolve the alert. The distribution list should not receive any notification of the acknowledgment.
* **Resolve alerts explicitly**
Alerts should resolve when the alert condition no longer applies. For example, if a replica set’s primary goes down, Platform issues an alert that the replica set does not have a primary. When a new primary is elected, the alert condition no longer applies, and the alert should resolve. 

# References

[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/architecture/design/platform-alerting-and-notification.md?pixel&useReferer)](https://github.com/yugabyte/ga-beacon)

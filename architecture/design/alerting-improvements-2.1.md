## Motivation
* Occassional flaky alerts from current script
* Current script may not catch issues properly in some corner cases (if either runs are delayed by a long time or if customers configure their alert check interval to long times).
* Potential integration of alerts with Slack/ServiceNow/SMS/etc.
* Customer asks

## Customer asks
* Ability to snooze alerts per universe
* Ability to disable alerts per universe
* Send out json formatted alerts for script consumption (?)
* HTTP endpoint to query cluster health (Already being done as part of https://github.com/yugabyte/yugabyte-db/issues/1834)
* Kubernetes ecosystem should preferably have health check endpoints or ports to decide whether to restart a pod or send traffic to a pod (currently we have these disabled).

## Plan
* Move alert functionality to prometheus/alertmanager over the next few releases
* Keep current ssh script check but instead of having the script email alerts directly, have yugaware convert the script check results to prometheus time series. Prometheus time series will look like 
```
universe_node_id_ssh_connectivity = 1/0
universe_node_id_cqlsh_connectivity = 1/0
universe_node_id_recent_core_dumps = 1/0
```
* Set up alertmanager alerts (via yugaware) based on these time series. An example could be: if sum(universe_node_id_cqlsh_connectivity) for last 5min < 2 (3 failures in 5 mins), raise an alert. 
* Note: In the future we could improve the quality of checks by doing them from outside the host instead of ssh-ing into the host.
* Kubernetes health checks
** Tservers: if TCP conn to port 5433/9100 is possible with a high timeout (60s) the server is healthy
** Master: if TCP conn to port 7100 is possible with a high timeout (60s) the server is healthy
* Once an alert is raised, platform provides a good overview to start debugging form. For open source, users possibly add a simple HTTP/JSON endpoint to master. The endpoint would show
** Number of under-replicated shards.
** Number of failing shards (< majority tserver alive)
** (Tracked in https://github.com/yugabyte/yugabyte-db/issues/1834)

## Implementation

* Mapping to alertmanager/prometheus concepts
** Snoozing alerts => amtool silence <alert_pattern> -d <duration> or POST-ing to the silences endpoint (http://petstore.swagger.io/?url=https://raw.githubusercontent.com/prometheus/alertmanager/master/api/v2/openapi.yaml)
** Alertmanager does not send out JSON emails but it supports a custom text/html template for emails which we can then configure with our own JSON format. One issue here is that our current email shows the entire state of universe health checks but alerts are typically more fine grained. On-call receiving an alert may need to log in to platform UI to view the full status.
** AlertManager only sends email on errors so we actually need to do additional work to send emails on success the way we do now. We may need to look into whether we need successful check emails at all.
* Tracking issue: https://github.com/yugabyte/yugabyte-db/issues/2894

### Pluses:
* Prometheus is a CNCF graduate, has very active usage. AlertManager has integrations with different notification mechanisms like Slack, IRC, SMS, ServiceNow apart from just email. Other visualization tools like Grafana can be integrated with Prometheus. There are also horizontally scalable backends for Prometheus (Uber M3) if that need arises.
* We can set up “pre-alerts” which could be noisier alerts that notify just the yugabyte team.
* Tweaking alerts is a constant process in any production environment. Doing this by tweaking alert queries is lot easier than rewriting our script to perform retries etc.

### Minuses:
* AlertManger and prometheus are still in significant flux, may have breaking changes.
* Upgrading to prometheus 2 involves breaking changes (we still run 1.8). 
* AlertManager APIs are not well documented.



### Notes from design meeting:

* TODO: Prometheus 2 upgrade important, front load the work?
** Can we run 1.8 and 2 side by side?
** Might be ok to lose 2 weeks worth of monitoring data as long as can access them in a pinch
* Alertmanager + prometheus version compatibility matrix?
* Health status is also shown in platform UI. Current plan is to keep health status in platform independent of alerts - it will show just the latest check while alerts may operate on a moving window to ride out flaky checks.
* Health check summary every 12 hours via email in platform will be retained
* Platform may need to check alertmanager at least initially
* TODO: Understand alert manager’s functionality to reload configs and other apis to query firing alerts 
* Kubernetes health check
** Understand what policies can be actions to happen for when the end points fail
** How can we ask k8s to move a pod off a node if the node has underlyng issues
* Master http endpoint - Bogdan and Nicolas are already working on something similar (https://github.com/yugabyte/yugabyte-db/issues/1834) 
* We have an existing alertmanager install that we could consult
* Ram has a diff for the prometheus upgrade



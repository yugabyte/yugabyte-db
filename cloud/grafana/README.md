# YugabyteDB Grafana Dashboard

To import the YugabyteDB Grafana dashboard, please see this [Grafana documentation link](https://grafana.com/docs/grafana/latest/reference/export_import/#importing-a-dashboard).

This dashboard was tested with Grafana v6.0.0 and v7.0.3.

## Best Practice:
- Set --metric_node_name flag in YugabyteDB configuration to get proper node name in YugabyteDB dashboard.
- YugabyteDB dashboard uses Prometheus' job name to separate multiple YugabyteDB clusters so, create the scrape jobs in Prometheus individually for every YugabyteDB cluster.

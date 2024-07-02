---
title: Use your own Prometheus instance
headerTitle: Use a custom Prometheus instance
linkTitle: Custom Prometheus
description: Export universe metrics to your own Prometheus instance
headcontent: Export universe metrics to your own Prometheus instance
menu:
  stable_yugabyte-platform:
    parent: alerts-monitoring
    identifier: prometheus-setup
    weight: 90
type: docs
---

While YugabyteDB Anywhere includes an [embedded Prometheus instance](../anywhere-metrics/) and uses it for storing time-series metrics data, you can also set up your own independent and separately-managed Prometheus instance.

This additional Prometheus instance can collect, visualize, alert on, and analyze universe metrics in your own observability tools. Prometheus itself, or any tool able to use Prometheus as a data source (such as Grafana) can be used.

Your independent Prometheus instance will scrape data from database nodes directly (in the case of VM-based universes) or scrape data from a K8s Prometheus Service Monitor (in the case of K8s-based universes). This data scraping runs in parallel to and independently from the YugabyteDB Anywhere-embedded Prometheus.

## Kubernetes

YugabyteDB allows scraping of universe metrics in Kubernetes using a ServiceMonitor. Similar to a Pod or Deployment, ServiceMonitor is also a resource that is handled by the Prometheus Operator. The ServiceMonitor resource selects a Kubernetes Service based on a given set of labels. With the help of this selected Service, the Prometheus Operator creates scrape configurations for Prometheus. Prometheus uses these configurations to actually scrape the application pods.

The ServiceMonitor resource can have details like port names or numbers, scrape interval, HTTP path where application is exposing the metrics, and so on. In the case of the `yugabytedb/yugabyte` Helm chart, it creates ServiceMonitors for YB-Master as well as YB-TServer pods.

### Prerequisites

Install the Prometheus Operator on your Kubernetes universe as follows:

```sh
kubectl get pods -n kube-prometheus-stack kube-prometheus-stack-operator-5577f9747-hqzbw
```

```output
NAME                                             READY   STATUS    RESTARTS   AGE
kube-prometheus-stack-operator-5577f9747-hqzbw   1/1     Running   0          4d19h
```

### Use Prometheus with a YugabyteDB Anywhere Kubernetes universe

To use a custom Prometheus instance with a universe on Kubernetes:

1. In YugabyteDB Anywhere, navigate to the universe you want to monitor.

1. Click **Actions** and choose **Edit Kubernetes Overrides**.

1. Add the following configuration:

    ```yaml
    serviceMonitor:
      enabled: true
      extraLabels:
        release: prom
    ```

    This configuration enables the service monitor and adds an extra label `release: prom`, which is required by the Prometheus Operator to discover this service monitor.

1. Click **Upgrade**.

To verify the configuration:

1. Access your Prometheus dashboard.

1. Navigate to the **Service Discovery** page.

1. Ensure that the service monitor for your YugabyteDB universe is listed.

To view metrics, use Prometheus queries. For sample queries, refer to [Analyze key metrics](../../../explore/observability/prometheus-integration/macos/#analyze-key-metrics)

If you have [Grafana](../../../explore/observability/grafana-dashboard/grafana/) available, you can access a rich set of visualizations using the [YugabyteDB Grafana dashboard](https://grafana.com/grafana/dashboards/12620-yugabytedb/).

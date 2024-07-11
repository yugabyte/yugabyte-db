---
title: Use your own Prometheus instance
headerTitle: Use a custom Prometheus instance
linkTitle: Custom Prometheus
description: Export universe metrics to your own Prometheus instance
headcontent: Export universe metrics to your own Prometheus instance
menu:
  stable_yugabyte-platform:
    parent: alerts-monitoring
    identifier: prometheus-custom
weight: 670
type: indexpage
---

Prometheus is a popular standard for time-series monitoring of cloud native infrastructure that treats time-series data as a data source for generating alerts. The format is easy to parse and can be massaged to work with other third-party monitoring systems like Grafana. While YugabyteDB Anywhere includes an [embedded Prometheus instance](../anywhere-metrics/) and uses it for storing time-series metrics data, you can also set up your own independent and separately-managed Prometheus instance.

You can use this additional Prometheus instance to collect, visualize, alert on, and analyze universe metrics in your own observability tools, whether in Prometheus itself, or any tool able to use Prometheus as a data source (such as Grafana).

You can do this in the following ways:

- [Federation](prometheus-federate/)

    Federation allows a Prometheus server to scrape selected time series from another Prometheus server. It is commonly used to either achieve scalable Prometheus monitoring setups or to pull related metrics from one service's Prometheus into another. All Yugabyte Platform aggregated metrics can be scraped into a different Prometheus system using built-in federation.

- [Scrape directly from universe nodes](prometheus-scrape/)

    Your independent Prometheus instance can scrape data from database nodes directly (in the case of VM-based universes) or scrape data from a Kubernetes Prometheus Operator Service Monitor (in the case of K8s-based universes). This data scraping runs in parallel to and independently from the YugabyteDB Anywhere-embedded Prometheus.

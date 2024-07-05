---
title: Federate metrics from YugabyteDB Anywhere
headerTitle: Federate metrics from YugabyteDB Anywhere
linkTitle: Federate metrics
description: Scrape metrics from the YugabyteDB Anywhere Prometheus server
headcontent: Scrape metrics from the YugabyteDB Anywhere Prometheus server
menu:
  stable_yugabyte-platform:
    parent: prometheus-custom
    identifier: prometheus-federate
    weight: 10
type: docs
---

To federate metrics from YugabyteDB Anywhere, configure your destination Prometheus server to scrape from the `/federate` endpoint of a YugabyteDB Anywhere source server, while also enabling the `honor_labels` scrape option (to not overwrite any labels exposed by the source server) and passing in the desired `match[]` parameters.

For example, to scrape all metrics from YugabyteDB Anywhere, the following `scrape_configs` federates any series with the label `job="yugabyte or platform or node"` or a metric name starting with `yugabyte/node/platform` from the YugabyteDB Anywhere Prometheus servers into the scraping Prometheus:

```yaml
# federate endpoint configuration
  - job_name: 'federate'
    scrape_interval: 15s
    # honor the labels from Platform's prometheus server if there's a label conflict
    honor_labels: true
    metrics_path: '/federate'
    params:
        # grab all node_exporter and platform metrics from two Yugabyte Platform nodes
        'match[]':
        - '{job="node"}'
        - '{job="platform"}'
        - '{job="yugabyte"}'
        # Use this match to grab ALL jobs instead of the ones above
        # - '{job=".+"}'
    static_configs:
      - targets:
        - 'xxx-yugaware.gcp:9090'
        - 'xxx-yugaware2.gcp:9090'
```

After this is configured, it would be possible to make API calls against this separate Prometheus server that would return metrics for both of the YugabyteDB Anywhere nodes in the configuration.

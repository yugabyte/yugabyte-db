// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { MetricsPanelOverview, DiskUsagePanel } from '../';
import './OverviewMetrics.scss';
import { YBLoading } from '../../common/indicators';
import { isNonEmptyObject, isNonEmptyArray, isEmptyArray, isNonEmptyString } from 'utils/ObjectUtils';
import { YBPanelLegend } from '../../common/descriptors';
import { YBWidget } from '../../panels';
import { METRIC_COLORS } from '../MetricsConfig';

const panelTypes = {
  overview: {title: "Overview",
    metrics: ["total_rpcs_per_sec",
      "tserver_ops_latency",
      "cpu_usage",
      "disk_usage"]}
};

class OverviewMetrics extends Component {
  static propTypes = {
    type: PropTypes.oneOf(Object.keys(panelTypes)).isRequired,
    nodePrefixes: PropTypes.array
  }

  static defaultProps = {
    nodePrefixes: []
  }

  componentDidMount() {
    this.queryMetricsType(this.props.graph.graphFilter);
  }

  queryMetricsType = graphFilter => {
    const {startMoment, endMoment, nodeName, nodePrefix} = graphFilter;
    const {type} = this.props;
    const params = {
      metrics: panelTypes[type].metrics,
      start: startMoment.format('X'),
      end: endMoment.format('X')
    };
    if (isNonEmptyString(nodePrefix) && nodePrefix !== "all") {
      params.nodePrefix = nodePrefix;
    }
    if (isNonEmptyString(nodeName) && nodeName !== "all") {
      params.nodeName = nodeName;
    }
    // In case of universe metrics , nodePrefix comes from component itself
    if (isNonEmptyArray(this.props.nodePrefixes)) {
      params.nodePrefix = this.props.nodePrefixes[0];
    }
    if (isNonEmptyString(this.props.tableName)) {
      params.tableName = this.props.tableName;
    }

    this.props.queryMetrics(params, type);
  };

  componentWillUnmount() {
    this.props.resetMetrics();
  }

  render() {
    const { type, graph: { metrics } } = this.props;
    let panelItem = panelTypes[type].metrics.filter((metricKey) => metricKey !== "disk_usage" && metricKey !== "cpu_usage").map(function(metricKey, idx) {
      return (<YBWidget key={type+idx}
        noMargin
        body={
          <YBLoading />
        }
      />);
    });
    if (Object.keys(metrics).length > 0 && isNonEmptyObject(metrics[type])) {
      /* Logic here is, since there will be multiple instances of GraphPanel
      we basically would have metrics data keyed off panel type. So we
      loop through all the possible panel types in the metric data fetched
      and group metrics by panel type and filter out anything that is empty.
      */
      const width = this.props.width;
      panelItem = panelTypes[type].metrics.map((metricKey, idx) => {
        // skip disk_usage and cpu_usage due to separate widget
        if(isNonEmptyObject(metrics[type][metricKey]) && !metrics[type][metricKey].error && (metricKey !== "disk_usage" || !this.props.layout) && metricKey !== "cpu_usage") {
          const legendData = [];
          for(let idx=0; idx < metrics[type][metricKey].data.length; idx++){
            metrics[type][metricKey].data[idx].line = {
              color: METRIC_COLORS[idx],
              width: 1.5
            };
            legendData.push({
              color: METRIC_COLORS[idx],
              title: metrics[type][metricKey].data[idx].name
            });
          }
          return (
            <YBWidget key={idx}
              noMargin
              headerRight={
                metricKey === "disk_usage" ? null :
                <YBPanelLegend data={legendData} />
              }
              headerLeft={metrics[type][metricKey].layout.title}
              body={metricKey === "disk_usage" ?
                <DiskUsagePanel
                  metric={metrics[type][metricKey]}
                  className={"disk-usage-container"}
                /> :
                <MetricsPanelOverview
                  metricKey={metricKey}
                  metric={metrics[type][metricKey]}
                  className={"metrics-panel-container"}
                  width={width}
                />
              }
            />
          );
        }
        return null;
      }).filter(Boolean);
    }
    let panelData= panelItem;
    if (isEmptyArray(panelItem)) {
      panelData = "Error receiving response from Graph Server";
    }
    return panelData;
  }
}

export default OverviewMetrics;

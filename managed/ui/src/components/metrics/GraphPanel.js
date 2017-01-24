// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Accordion, Panel, Col } from 'react-bootstrap';

import { MetricsPanel } from '.'
import './stylesheets/GraphPanel.scss'

const panelTypes = {
  redis:   { title: "Redis Stats",
             metrics: ["redis_ops_latency", "redis_rpcs_per_sec"]},
  tserver: { title: "TServer Stats",
             metrics: ["tserver_ops_latency", "tserver_rpcs_per_sec"]},
  server:  { title: "YB Server Stats",
             metrics: ["cpu_usage", "memory_usage", "disk_iops", "network_bytes", "system_load_over_time",
                       "network_packets", "network_errors", "disk_bytes_per_second_per_node"]}
}

export default class GraphPanel extends Component {
  static propTypes = {
    type: PropTypes.oneOf(Object.keys(panelTypes)).isRequired,
    nodePrefixes: PropTypes.array
  }

  static defaultProps = {
    nodePrefixes: []
  }

  componentWillReceiveProps(nextProps) {
    // Perform metric query only if the graph filter has changed.
    // TODO: add the nodePrefixes to the queryParam
    if(nextProps.graph.graphFilter !== this.props.graph.graphFilter) {
      const { type, graph: {graphFilter: {startDate, endDate}}} = nextProps;
      var params = {
        metrics: panelTypes[type].metrics,
        start: startDate,
        end: endDate
      }
      this.props.queryMetrics(params, type);
    }
  }

  componentWillUnmount() {
    this.props.resetMetrics();
  }

  render() {
    const { type, graph: { loading, metrics }} = this.props;
    var metricsPanelsByType = []

    if (!loading && Object.keys(metrics).length > 0) {
      /* Logic here is, since there will be multiple instances of GraphPanel
         we basically would have metrics data keyed off panel type. So we
         loop through all the possible panel types in the metric data fetched
         and group metrics by panel type and filter out anything that is empty.
      */
      metricsPanelsByType = Object.keys(metrics).map(function(panelType) {
        var panelMetrics = metrics[panelType]
        var metricsPanels = Object.keys(panelMetrics).map(function(key) {
          if (panelTypes[type].metrics.includes(key)) {
            return (
              <Col lg={4} key={key} >
                <MetricsPanel metricKey={key} metric={panelMetrics[key]} className={"metrics-panel-container"}/>
              </Col>
            )
          }
          return null
        }).filter(Boolean);
        if (metricsPanels.length > 0) {
          const panelHeader = <h3>{panelTypes[panelType].title}</h3>
          return (
            <Panel header={panelHeader} key={panelType} className="metrics-container">
              {metricsPanels}
            </Panel>
          );
        }
        return null
      }).filter(Boolean);
      
      return (
        <Accordion>
          {metricsPanelsByType}
        </Accordion>
      );
    }
    // TODO: add loading indicator
    return (<div />)
  }
}

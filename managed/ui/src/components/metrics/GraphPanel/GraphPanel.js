// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Accordion, Panel, Col, Row } from 'react-bootstrap';
import { MetricsPanel } from '../../metrics';
import './GraphPanel.scss';
import {isValidObject, isValidArray} from '../../../utils/ObjectUtils';

const panelTypes = {
  server:  { title: "YB Server Stats",
             metrics: ["memory_usage", "disk_iops", "cpu_usage", "network_bytes", "system_load_over_time",
                       "network_packets", "network_errors", "disk_bytes_per_second_per_node"]},
  tserver: { title: "TServer Stats",
             metrics: ["tserver_ops_latency", "tserver_rpcs_per_sec", "tserver_cache_ops_per_sec","tserver_threads",
              "tserver_change_config", "tserver_context_switches", "tserver_spinlock_server", "tserver_log_latency",
              "tserver_log_bytes_written", "tserver_log_bytes_read", "tserver_log_ops_second", "tserver_tc_malloc_stats",
              "tserver_log_stats", "tserver_cache_reader_num_ops"]},
  redis:   { title: "Redis Stats",
             metrics: ["redis_ops_latency", "redis_rpcs_per_sec"]}
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

    if (!loading && Object.keys(metrics).length > 0 && isValidObject(metrics[type])) {
      /* Logic here is, since there will be multiple instances of GraphPanel
       we basically would have metrics data keyed off panel type. So we
       loop through all the possible panel types in the metric data fetched
       and group metrics by panel type and filter out anything that is empty.
       */
      var panelItem = panelTypes[type].metrics.map(function(metricKey) {
        if (isValidObject(metrics[type][metricKey]) && isValidArray(metrics[type][metricKey].data)) {
          return <Col lg={4} key={metricKey}>
            <MetricsPanel metricKey={metricKey} metric={metrics[type][metricKey]}
                          className={"metrics-panel-container"}/>
             </Col>
          } else {
          return null
          }
      });
      return (
        <Accordion>
          <Panel header={panelTypes[type].title} key={panelTypes[type]} className="metrics-container">
            {panelItem}
          </Panel>
        </Accordion>
      );
    }

    return (<div />)
  }
}

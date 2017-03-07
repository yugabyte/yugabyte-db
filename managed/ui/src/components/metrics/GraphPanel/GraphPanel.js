// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Accordion, Panel } from 'react-bootstrap';
import Dimensions from 'react-dimensions';
import { MetricsPanel } from '../../metrics';
import './GraphPanel.scss';
import {isValidObject, isValidArray} from '../../../utils/ObjectUtils';

const RESIZE_DEBOUNCE_MS = 100;

const panelTypes = {
  server:  { title: "Node",
             metrics: ["cpu_usage", "memory_usage", "disk_iops", "disk_bytes_per_second_per_node",
                       "network_bytes", "network_packets", "network_errors", "system_load_over_time"]},
  tserver: { title: "YugaByte Server",
             metrics: ["tserver_ops_latency", "tserver_rpcs_per_sec", "tserver_cache_ops_per_sec","tserver_threads",
              "tserver_change_config", "tserver_context_switches", "tserver_spinlock_server", "tserver_log_latency",
              "tserver_log_bytes_written", "tserver_log_bytes_read", "tserver_log_ops_second", "tserver_tc_malloc_stats",
              "tserver_log_stats", "tserver_cache_reader_num_ops"]},
  redis:   { title: "Redis",
             metrics: ["redis_ops_latency", "redis_rpcs_per_sec"]},
  cql:     { title: "Cassandra",
             metrics: ["cql_server_rpc_per_second", "cql_sql_latency", "cql_yb_latency", "response_sizes"]}
}

class GraphPanel extends Component {
  constructor(props) {
    super(props);
    this.queryMetricsType = this.queryMetricsType.bind(this);
  }
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

  componentWillReceiveProps(nextProps) {
    // Perform metric query only if the graph filter has changed.
    // TODO: add the nodePrefixes to the queryParam
    if(nextProps.graph.graphFilter !== this.props.graph.graphFilter) {
      this.queryMetricsType(nextProps.graph.graphFilter);
    }
  }

  queryMetricsType(graphFilter) {
    const {startMoment, endMoment, nodeName, nodePrefix} = graphFilter;
    const {type} = this.props;
    var params = {
      metrics: panelTypes[type].metrics,
      start: startMoment.format('X'),
      end: endMoment.format('X')
    }
    if (isValidObject(nodePrefix) && nodePrefix !== "all") {
      params.nodePrefix = nodePrefix;
    }
    if (isValidObject(nodeName) && nodeName !== "all") {
      params.nodeName = nodeName;
    }
    // In case of universe metrics , nodePrefix comes from component itself
    if (isValidArray(this.props.nodePrefixes)) {
      params.nodePrefix = this.props.nodePrefixes[0];
    }

    this.props.queryMetrics(params, type);
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
      const containerWidth = this.props.containerWidth;
      var panelItem = panelTypes[type].metrics.map(function(metricKey, idx) {
        // if (isValidObject(metrics[type][metricKey]) && isValidArray(metrics[type][metricKey].data)) {
        return (isValidObject(metrics[type][metricKey])) ?
          <MetricsPanel metricKey={metricKey} key={idx}
                        metric={metrics[type][metricKey]}
                        className={"metrics-panel-container"}
                        containerWidth={containerWidth} />
          : null;
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

export default Dimensions({debounce: RESIZE_DEBOUNCE_MS, elementResize: true})(GraphPanel);

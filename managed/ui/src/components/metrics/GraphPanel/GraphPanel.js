// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { Accordion, Panel } from 'react-bootstrap';
import { MetricsPanel } from '../../metrics';
import './GraphPanel.scss';
import {YBLoadingIcon} from '../../common/indicators';
import { isNonEmptyObject, isNonEmptyArray, isEmptyArray, isNonEmptyString } from 'utils/ObjectUtils';

const panelTypes = {
  server:  { title: "Node",
    metrics: ["cpu_usage", "memory_usage", "disk_iops", "disk_bytes_per_second_per_node",
      "network_bytes", "network_packets", "network_errors", "system_load_over_time"]},
  tserver: { title: "YugaByte Server",
    metrics: [ "tserver_rpcs_per_sec",  "tserver_write_latency", "tserver_read_latency", "tserver_ops_latency", "tserver_handler_latency", "tserver_threads",
      "tserver_consensus_rpcs_per_sec", "tserver_change_config", "tserver_remote_bootstraps", "tserver_consensus_rpcs_latency", "tserver_change_config_latency", "tserver_context_switches", "tserver_spinlock_server", "tserver_log_latency",
      "tserver_log_bytes_written", "tserver_log_bytes_read", "tserver_log_ops_second", "tserver_tc_malloc_stats",
      "tserver_log_stats", "tserver_cache_reader_num_ops"]},
  lsmdb: { title: "LSM-DB Stats",
    metrics: ["lsm_rocksdb_num_seek_or_next",
      "lsm_rocksdb_num_seeks_per_node",
      "lsm_rocksdb_latencies_get",
      "lsm_rocksdb_latencies_write",
      "lsm_rocksdb_latencies_seek",
      "lsm_rocksdb_latencies_mutex",
      "lsm_rocksdb_block_cache_hit_miss",
      "lsm_rocksdb_block_cache_usage",
      "lsm_rocksdb_blooms_checked_and_useful",
      "lsm_rocksdb_stalls",
      "lsm_rocksdb_flush_size",
      "lsm_rocksdb_compaction",
      "lsm_rocksdb_compaction_time",
      "lsm_rocksdb_compaction_numfiles"]},
  proxies:   { title: "Apache Cassandra and Redis",
    metrics: ["cql_server_rpc_per_second",  "cql_sql_latency", "memory_usage_all", "redis_rpcs_per_sec", "redis_ops_latency"]},
  redis:   { title: "Redis Advanced Graphs",
    metrics: ["redis_yb_local_vs_remote", "redis_yb_latency", "redis_reactor_latency"]},
  cql:     { title: "Apache Cassandra Advanced Graphs",
    metrics: ["cql_sql_latency_breakdown", "cql_yb_local_vs_remote", "cql_yb_latency", "cql_reactor_latency", "response_sizes"]}
};

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
      this.props.resetMetrics();
      this.queryMetricsType(nextProps.graph.graphFilter);
    }
  }

  queryMetricsType(graphFilter) {
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

    this.props.queryMetrics(params, type);
  }

  componentWillUnmount() {
    this.props.resetMetrics();
  }

  render() {
    const { type, graph: { metrics }} = this.props;

    let panelItem = <YBLoadingIcon />;
    if (Object.keys(metrics).length > 0 && isNonEmptyObject(metrics[type])) {
      /* Logic here is, since there will be multiple instances of GraphPanel
      we basically would have metrics data keyed off panel type. So we
      loop through all the possible panel types in the metric data fetched
      and group metrics by panel type and filter out anything that is empty.
      */
      const width = this.props.width;
      panelItem = panelTypes[type].metrics.map(function(metricKey, idx) {
        return (isNonEmptyObject(metrics[type][metricKey]) && !metrics[type][metricKey].error) ?
          <MetricsPanel metricKey={metricKey} key={idx}
                        metric={metrics[type][metricKey]}
                        className={"metrics-panel-container"}
                        width={width} />
          : null;
      }).filter(Boolean);
    }
    let panelData= panelItem;
    if (isEmptyArray(panelItem)) {
      panelData = "Error receiving response from Graph Server";
    }
    return (
      <Accordion>
        <Panel header={panelTypes[type].title} key={panelTypes[type]} className="metrics-container">
          {panelData}
        </Panel>
      </Accordion>
    );
  }
}

export default GraphPanel;

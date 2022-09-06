// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { Panel } from 'react-bootstrap';
import { MetricsPanel } from '../../metrics';
import './GraphPanel.scss';
import { YBLoading } from '../../common/indicators';
import {
  isNonEmptyObject,
  isEmptyObject,
  isNonEmptyArray,
  isEmptyArray,
  isNonEmptyString
} from '../../../utils/ObjectUtils';
import { isKubernetesUniverse } from '../../../utils/UniverseUtils';
import { YUGABYTE_TITLE } from '../../../config';

export const panelTypes = {
  container: {
    title: 'Container',
    metrics: ['container_cpu_usage', 'container_memory_usage', 'container_volume_stats']
  },
  server: {
    title: 'Node',
    metrics: [
      'cpu_usage',
      'memory_usage',
      'disk_iops',
      'disk_bytes_per_second_per_node',
      'network_packets',
      'network_bytes',
      'network_errors',
      'system_load_over_time',
      'node_clock_skew'
    ]
  },
  tserver: {
    title: 'Tablet Server',
    metrics: [
      'tserver_rpcs_per_sec_per_node',
      'tserver_ops_latency',
      'tserver_handler_latency',
      'tserver_threads_running',
      'tserver_threads_started',
      'tserver_consensus_rpcs_per_sec',
      'tserver_change_config',
      'tserver_remote_bootstraps',
      'tserver_consensus_rpcs_latency',
      'tserver_change_config_latency',
      'tserver_context_switches',
      'tserver_spinlock_server',
      'tserver_log_latency',
      'tserver_log_bytes_written',
      'tserver_log_bytes_read',
      'tserver_log_ops_second',
      'tserver_tc_malloc_stats',
      'tserver_log_stats',
      'tserver_cache_reader_num_ops',
      'tserver_glog_info_messages',
      'tserver_rpc_queue_size_tserver',
      'tserver_cpu_util_secs',
      'tserver_yb_rpc_connections'
    ]
  },
  master: {
    title: 'Master Server',
    metrics: [
      'master_overall_rpc_rate',
      'master_latency',
      'master_get_tablet_location',
      'master_tsservice_reads',
      'master_tsservice_reads_latency',
      'master_tsservice_writes',
      'master_tsservice_writes_latency',
      'master_ts_heartbeats',
      'tserver_rpc_queue_size_master',
      'master_consensus_update',
      'master_consensus_update_latency',
      'master_multiraft_consensus_update',
      'master_multiraft_consensus_update_latency',
      'master_table_ops',
      'master_cpu_util_secs',
      'master_yb_rpc_connections'
    ]
  },
  master_advanced: {
    title: 'Master Server Advanced',
    metrics: [
      'master_threads_running',
      'master_log_latency',
      'master_log_bytes_written',
      'master_log_bytes_read',
      'master_tc_malloc_stats',
      'master_glog_info_messages',
      'master_lsm_rocksdb_num_seek_or_next',
      'master_lsm_rocksdb_num_seeks_per_node',
      'master_lsm_rocksdb_total_sst_per_node',
      'master_lsm_rocksdb_avg_num_sst_per_node',
      'master_lsm_rocksdb_block_cache_hit_miss',
      'master_lsm_rocksdb_block_cache_usage',
      'master_lsm_rocksdb_blooms_checked_and_useful',
      'master_lsm_rocksdb_flush_size',
      'master_lsm_rocksdb_compaction',
      'master_lsm_rocksdb_compaction_numfiles',
      'master_lsm_rocksdb_compaction_time'
    ]
  },
  lsmdb: {
    title: 'DocDB',
    metrics: [
      'lsm_rocksdb_num_seek_or_next',
      'lsm_rocksdb_num_seeks_per_node',
      'lsm_rocksdb_total_sst_per_node',
      'lsm_rocksdb_avg_num_sst_per_node',
      'lsm_rocksdb_latencies_get',
      'lsm_rocksdb_latencies_write',
      'lsm_rocksdb_latencies_seek',
      'lsm_rocksdb_latencies_mutex',
      'lsm_rocksdb_block_cache_hit_miss',
      'lsm_rocksdb_block_cache_usage',
      'lsm_rocksdb_blooms_checked_and_useful',
      'lsm_rocksdb_stalls',
      'lsm_rocksdb_write_rejections',
      'lsm_rocksdb_flush_size',
      'lsm_rocksdb_compaction',
      'lsm_rocksdb_compaction_time',
      'lsm_rocksdb_compaction_numfiles',
      'docdb_transaction',
      'docdb_transaction_pool_cache'
    ]
  },
  ysql_ops: {
    title: 'YSQL Ops and Latency',
    metrics: [
      'ysql_server_rpc_per_second',
      'ysql_sql_latency',
      'ysql_connections'
      // TODO(bogdan): Add these in once we have histogram support, see #3630.
      // "ysql_server_rpc_p99"
    ]
  },
  ycql_ops: {
    title: 'YCQL Ops and Latency',
    metrics: ['cql_server_rpc_per_second', 'cql_sql_latency', 'cql_server_rpc_p99']
  },
  yedis_ops: {
    title: 'YEDIS Ops and Latency',
    metrics: ['redis_rpcs_per_sec_all', 'redis_ops_latency_all', 'redis_server_rpc_p99']
  },
  redis: {
    title: 'YEDIS Advanced',
    metrics: [
      'redis_yb_local_vs_remote_ops',
      'tserver_rpc_queue_size_redis',
      'redis_yb_local_vs_remote_latency',
      'redis_reactor_latency',
      'redis_rpcs_per_sec_hash',
      'redis_ops_latency_hash',
      'redis_rpcs_per_sec_ts',
      'redis_ops_latency_ts',
      'redis_rpcs_per_sec_set',
      'redis_ops_latency_set',
      'redis_rpcs_per_sec_sortedset',
      'redis_ops_latency_sorted_set',
      'redis_rpcs_per_sec_str',
      'redis_ops_latency_str',
      'redis_rpcs_per_sec_local',
      'redis_ops_latency_local',
      'redis_yb_rpc_connections'
    ]
  },

  sql: {
    title: 'YSQL Advanced',
    metrics: ['ysql_server_advanced_rpc_per_second', 'ysql_sql_advanced_latency']
  },

  cql: {
    title: 'YCQL Advanced',
    metrics: [
      'cql_sql_latency_breakdown',
      'cql_yb_local_vs_remote',
      'cql_yb_latency',
      'cql_reactor_latency',
      'tserver_rpc_queue_size_cql',
      'response_sizes',
      'cql_yb_transaction',
      'cql_yb_rpc_connections'
    ]
  },

  tserver_table: {
    title: 'Tablet Server',
    metrics: [
      'tserver_log_latency',
      'tserver_log_bytes_written',
      'tserver_log_bytes_read',
      'tserver_log_ops_second',
      'tserver_log_stats',
      'tserver_cache_reader_num_ops'
    ]
  },

  lsmdb_table: {
    title: 'DocDB',
    metrics: [
      'lsm_rocksdb_num_seek_or_next',
      'lsm_rocksdb_num_seeks_per_node',
      'lsm_rocksdb_total_sst_per_node',
      'lsm_rocksdb_avg_num_sst_per_node',
      'lsm_rocksdb_latencies_get',
      'lsm_rocksdb_latencies_write',
      'lsm_rocksdb_latencies_seek',
      'lsm_rocksdb_block_cache_hit_miss',
      'lsm_rocksdb_blooms_checked_and_useful',
      'lsm_rocksdb_stalls',
      'lsm_rocksdb_flush_size',
      'lsm_rocksdb_compaction',
      'lsm_rocksdb_compaction_time',
      'lsm_rocksdb_compaction_numfiles',
      'docdb_transaction'
    ]
  }
};

class GraphPanel extends Component {
  constructor(props) {
    super(props);
    this.state = { isOpen: false, ...this.props };
  }

  static propTypes = {
    type: PropTypes.oneOf(Object.keys(panelTypes)).isRequired,
    nodePrefixes: PropTypes.array
  };

  static defaultProps = {
    nodePrefixes: []
  };

  componentDidMount() {
    if (this.state.isOpen) {
      this.queryMetricsType(this.props.graph.graphFilter);
    }
  }

  queryMetricsType = (graphFilter) => {
    const { startMoment, endMoment, nodeName, nodePrefix } = graphFilter;
    const { type } = this.props;
    const splitTopNodes = (isNonEmptyString(nodeName) && nodeName === 'top') ? 1 : 0;
    const metricsWithSettings = panelTypes[type].metrics.map((metric) =>
     {
        return {
          metric: metric,
          splitTopNodes: splitTopNodes
        }
     })
    const params = {
      metricsWithSettings: metricsWithSettings,
      start: startMoment.format('X'),
      end: endMoment.format('X')
    };
    if (isNonEmptyString(nodePrefix) && nodePrefix !== 'all') {
      params.nodePrefix = nodePrefix;
    }
    if (isNonEmptyString(nodeName) && nodeName !== 'all' && nodeName !== 'top') {
      params.nodeNames = [nodeName];
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

  componentDidUpdate(prevProps, prevState) {
    // Perform metric query only if the graph filter has changed.
    // TODO: add the nodePrefixes to the queryParam
    if (prevProps.graph.graphFilter !== this.props.graph.graphFilter) {
      if (this.state.isOpen) {
        this.queryMetricsType(this.props.graph.graphFilter);
      }
    }

    const {
      type,
      graph: { metrics }
    } = this.props;
    if (
      !prevState.isOpen &&
      this.state.isOpen &&
      (Object.keys(metrics).length === 0 || isEmptyObject(metrics[type]))
    ) {
      this.queryMetricsType(this.props.graph.graphFilter);
    }
  }

  render() {
    const {
      type,
      selectedUniverse,
      insecureLoginToken,
      graph: { metrics, prometheusQueryEnabled },
      customer: { currentUser }
    } = this.props;
    const { nodeName } = this.props.graph.graphFilter;

    let panelData = <YBLoading />;

    if (
      insecureLoginToken &&
      !(type === 'ycql_ops' || type === 'ysql_ops' || type === 'yedis_ops')
    ) {
      panelData = (
        <div className="oss-unavailable-warning">Only available on {YUGABYTE_TITLE}.</div>
      );
    } else {
      if (Object.keys(metrics).length > 0 && isNonEmptyObject(metrics[type])) {
        /* Logic here is, since there will be multiple instances of GraphPanel
        we basically would have metrics data keyed off panel type. So we
        loop through all the possible panel types in the metric data fetched
        and group metrics by panel type and filter out anything that is empty.
        */
        const width = this.props.width;
        panelData = panelTypes[type].metrics
          .map(function (metricKey, idx) {
            return isNonEmptyObject(metrics[type][metricKey]) && !metrics[type][metricKey].error ? (
              <MetricsPanel
                currentUser={currentUser}
                metricKey={metricKey}
                key={idx}
                metric={metrics[type][metricKey]}
                className={'metrics-panel-container'}
                containerWidth={width}
                prometheusQueryEnabled={prometheusQueryEnabled}
              />
            ) : null;
          })
          .filter(Boolean);
      }
      const invalidPanelType =
        selectedUniverse && isKubernetesUniverse(selectedUniverse)
          ? panelTypes[type].title === 'Node'
          : panelTypes[type].title === 'Container';
      if (invalidPanelType) {
        return null;
      }

      if (selectedUniverse && isKubernetesUniverse(selectedUniverse)) {
        //Hide master related panels for tserver pods.
        if (nodeName.match('yb-tserver-') != null) {
          if (panelTypes[type].title === 'Master Server' || panelTypes[type].title === 'Master Server Advanced'){
            return null;
          }
        }
        //Hide empty panels for master pods.
        if (nodeName.match('yb-master-') != null) {
          const skipList = ['Tablet Server',
            'YSQL Ops and Latency',
            'YCQL Ops and Latency',
            'YEDIS Ops and Latency',
            'YEDIS Advanced',
            'YSQL Advanced',
            'YCQL Advanced']
          if (skipList.includes(panelTypes[type].title)) {
            return null;
          }
        }
      }

      if (isEmptyArray(panelData)) {
        panelData = 'Error receiving response from Graph Server';
      }
    }

    return (
      <Panel
        id={panelTypes[type].title}
        key={panelTypes[type]}
        eventKey={this.props.eventKey}
        defaultExpanded={this.state.isOpen}
        className="metrics-container"
      >
        <Panel.Heading>
          <Panel.Title
            tag="h4"
            toggle
            onClick={() => {
              this.setState({ isOpen: !this.state.isOpen });
            }}
          >
            {panelTypes[type].title}
          </Panel.Title>
        </Panel.Heading>
        <Panel.Body collapsible>{panelData}</Panel.Body>
      </Panel>
    );
  }
}

export default GraphPanel;

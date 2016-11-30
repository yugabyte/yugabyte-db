// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
var Plotly = require('plotly.js/lib/core');
import { removeNullProperties } from '../utils/ObjectUtils';

export default class MetricsPanel extends Component {
  constructor(props) {
    super(props);
    this.state = {
      queryMetrics: true
    }
    this.applyFilter = this.applyFilter.bind(this);
  }

  static propTypes = {
    metricKey: PropTypes.oneOf(['cpu_usage', 'cpu_usage_user', 'cpu_usage_system',
    'memory_usage', 'cpu_usage_system_avg', 'redis_ops_latency', 'redis_rpcs_per_sec']),
    origin: PropTypes.oneOf(['customer', 'universe']).isRequired,
    universeUUID: PropTypes.string
  }

  static defaultProps = {
    universeUUID: null
  }

  componentWillReceiveProps(nextProps) {
    if (this.props.graph.graphFilter !== nextProps.graph.graphFilter) {
      this.applyFilter(nextProps.graph.graphFilter);
    }
  }

  componentDidMount() {
    const { graphFilter } = this.props.graph;
    this.applyFilter(graphFilter);
  }

  componentWillUnmount() {
    this.props.resetMetrics();
  }


  applyFilter(filterParams) {
    const { origin, universeUUID, metricKey } = this.props;
    var params = {
      metricKey: metricKey,
      start: filterParams.startDate.format('X'),
      end: filterParams.endDate.format('X')
    };

    if (origin === "customer") {
      // Fetch customer metrics
      this.props.queryCustomerMetrics(params);
    } else if (origin === "universe" && universeUUID !== null) {
      // Fetch universe metrics
      this.props.queryUniverseMetrics(universeUUID, params);
    }

  }

  render() {
    const { metricKey, graph: { loading, metrics }} = this.props;
    if (typeof metrics === "undefined" || loading ) {
      return (
        <div id={metricKey} />
      )
    }
    // Remove Null Properties from the layout 
    removeNullProperties(metrics.layout);
    if (typeof metrics !== "undefined" && metrics.metricKey === metricKey) {
      Plotly.newPlot(metricKey, metrics.data, metrics.layout, {displayModeBar: false});
    }

    return (
      <div id={metricKey} />
    );
  }
}

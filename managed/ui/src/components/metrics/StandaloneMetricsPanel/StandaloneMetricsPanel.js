// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { YBLoading } from '../../common/indicators';
import { isNonEmptyObject,  } from 'utils/ObjectUtils';
import { METRIC_COLORS } from '../MetricsConfig';
import { YBWidget } from '../../panels';

class StandaloneMetricsPanel extends Component {
  static propTypes = {
    type: PropTypes.string.isRequired,
    metricKey: PropTypes.string.isRequired,
    width: PropTypes.number,
    children: PropTypes.func.isRequired
  }

  render() {
    const { type, graph: { metrics }, metricKey, width } = this.props;
    const props = {};
    props.metricKey = metricKey;
    props.width = width;
    if (Object.keys(metrics).length > 0 && isNonEmptyObject(metrics[type]) && isNonEmptyObject(metrics[type][metricKey])&& !metrics[type][metricKey].error) {
      /* Logic here is that some other main component 
      like OverviewMetrics or CustomerMetricsPanel is capabale of loading data
      and this panel only displays relevant data for the metric 
      that should be displayed separately  
      */
      const legendData = [];
      for(let idx=0; idx < metrics[type][metricKey].data.length; idx++){
        metrics[type][metricKey].data[idx].fill = "tozeroy";
        metrics[type][metricKey].data[idx].fillcolor = METRIC_COLORS[idx]+"10";
        metrics[type][metricKey].data[idx].line = {
          color: METRIC_COLORS[idx],
          width: 1.5
        };
        legendData.push({
          color: METRIC_COLORS[idx],
          title: metrics[type][metricKey].data[idx].name
        });
      }

      props.metric = metrics[type][metricKey];
    } else {
      props.metric = {
        data: [],
        layout: {title: metricKey, xaxis: {}, yaxis: {}},
        queryKey: metricKey
      };
      return (<YBWidget
        noMargin
        body={
          <YBLoading />
        }
      />);
    }
    return this.props.children(props);
  }
}

export default StandaloneMetricsPanel;

// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
var Plotly = require('plotly.js/lib/core');
import { removeNullProperties, isValidObject, isValidArray } from '../../../utils/ObjectUtils';
import './MetricsPanel.scss';

export default class MetricsPanel extends Component {

  static propTypes = {
    metric: PropTypes.object.isRequired,
    metricKey: PropTypes.string.isRequired
  }
  componentDidMount() {
    const { metricKey, metric } = this.props;
    if (isValidObject(metric)) {
      // Remove Null Properties from the layout
      removeNullProperties(metric.layout);

      // TODO: send this data from backend.
      metric.layout.autosize = false;
      metric.layout.width = 400;
      metric.layout.height = 360;
      metric.layout.showlegend = true;
      metric.layout.yaxis = {rangemode: "nonnegative"}
      metric.layout.legend = {xanchor:"center", yanchor:"top",
                              y:-0.3, x:0.5, orientation: "h"}
      metric.layout.margin = {
        l: 45,
        r: 25,
        b: 45,
        t: 70,
        pad: 4,
      };

      // Handle the case when the metric data is empty, we would show
      // graph with No Data annotation.
      if (!isValidArray(metric.data)) {
        metric.layout["annotations"] = [{
          visible: true,
          align: "center",
          text: "No Data",
          showarrow: false,
          x: 1,
          y: 1
        }];
        metric.layout["xaxis"] = {range: [0, 2]}
        metric.layout["yaxis"] = {range: [0, 2]}
      }

      Plotly.newPlot(metricKey, metric.data, metric.layout, {displayModeBar: false});
    }
  }

  render() {
    return (
      <div id={this.props.metricKey} className="metrics-panel" />
    );
  }
}

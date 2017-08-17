// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { removeNullProperties, isNonEmptyObject, isNonEmptyArray } from 'utils/ObjectUtils';
import './MetricsPanel.scss';

var Plotly = require('plotly.js/lib/core');

const WIDTH_OFFSET = 5;
const MAX_GRAPH_WIDTH_PX = 600;
const GRAPH_GUTTER_WIDTH_PX = 15;

export default class MetricsPanel extends Component {
  static propTypes = {
    metric: PropTypes.object.isRequired,
    metricKey: PropTypes.string.isRequired
  }

  componentDidMount() {
    const { metricKey, metric } = this.props;
    if (isNonEmptyObject(metric)) {
      // Remove Null Properties from the layout
      removeNullProperties(metric.layout);

      // TODO: send this data from backend.
      var max = 0;
      metric.data.forEach(function (data) {
        if (data.y) {
          data.y.forEach(function (y) {
            y = parseFloat(y) * 1.25;
            if (y > max) max = y;
          });
        }
      });
      if (max === 0) max = 1.01;
      metric.layout.autosize = false;
      metric.layout.width = this.getGraphWidth(this.props.width || 1200);
      metric.layout.height = 360;
      metric.layout.showlegend = true;
      metric.layout.margin = {
        l: 45,
        r: 25,
        b: 0,
        t: 70,
        pad: 4,
      };
      if (isNonEmptyObject(metric.layout.yaxis) && isNonEmptyObject(metric.layout.yaxis.ticksuffix)) {
        metric.layout.margin.l = 70;
        metric.layout.yaxis.range = [0, max];
      } else {
        metric.layout.yaxis = {range: [0, max]};
      }
      metric.layout.font = {
        family: 'Helvetica Neue, Helvetica, Roboto, Arial, Droid Sans, sans-serif',
      };
      metric.layout.legend = {
        orientation: "h",
        xanchor: "center",
        yanchor: "bottom",
        x: 0.5,
        y: -0.5,
      };

      // Handle the case when the metric data is empty, we would show
      // graph with No Data annotation.
      if (!isNonEmptyArray(metric.data)) {
        metric.layout["annotations"] = [{
          visible: true,
          align: "center",
          text: "No Data",
          showarrow: false,
          x: 1,
          y: 1
        }];
        metric.layout.margin.b = 105;
        metric.layout.xaxis = {range: [0, 2]};
        metric.layout.yaxis = {range: [0, 2]};
      }

      Plotly.newPlot(metricKey, metric.data, metric.layout, {displayModeBar: false});
    }
  }

  componentWillReceiveProps(newProps) {
    if (newProps.width !== this.props.width) {
      Plotly.relayout(this.props.metricKey, {width: this.getGraphWidth(newProps.width)});
    }
  }

  getGraphWidth(containerWidth) {
    var width = containerWidth - WIDTH_OFFSET;
    var columnCount = Math.ceil(width / MAX_GRAPH_WIDTH_PX);
    return Math.floor(width / columnCount) - GRAPH_GUTTER_WIDTH_PX;
  }

  render() {
    return (
      <div id={this.props.metricKey} className="metrics-panel">
        <div />
      </div>
    );
  }
}

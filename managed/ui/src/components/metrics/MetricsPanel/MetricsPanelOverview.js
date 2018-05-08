// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { isNonEmptyObject, isNonEmptyArray, isNonEmptyString } from 'utils/ObjectUtils';
import './MetricsPanel.scss';
import _ from 'lodash';
import { METRIC_FONT } from '../MetricsConfig';

const Plotly = require('plotly.js/lib/core');

const WIDTH_OFFSET = 0;
const MAX_GRAPH_WIDTH_PX = 600;
const GRAPH_GUTTER_WIDTH_PX = 15;

export default class MetricsPanelOverview extends Component {
  static propTypes = {
    metric: PropTypes.object.isRequired,
    metricKey: PropTypes.string.isRequired
  }

  componentDidMount() {
    const { metricKey } = this.props;
    const metric = _.cloneDeep(this.props.metric);
    if (isNonEmptyObject(metric)) {

      // TODO: send this data from backend.
      let max = 0;
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
      metric.layout.height = 135;
      metric.layout.title = "";
      metric.layout.showlegend = false;
      metric.layout.margin = {
        l: 0,
        r: 0,
        b: 0,
        t: 0,
        pad: 0,
      };
      if (isNonEmptyObject(metric.layout.yaxis) && isNonEmptyString(metric.layout.yaxis.ticksuffix)) {
        metric.layout.margin.l = 40;
        metric.layout.yaxis.range = [0, max];
      } else {
        metric.layout.yaxis = {range: [0, max]};
      }
      metric.layout.yaxis.fixedrange = true;
      metric.layout.xaxis.fixedrange = true;
      metric.layout.yaxis._offset = 10;
      metric.layout.font = {
        family: METRIC_FONT,
        weight: 300
      };
      metric.layout.margin = {
        l: 0,
        r: 0,
        b: 16,
        t: 0,
      };
      metric.layout.xaxis = {...metric.layout.xaxis, ...{ color: '#444444', zerolinecolor: '#000', gridcolor: '#eee' }};
      metric.layout.yaxis = {...metric.layout.yaxis, ...{ color: '#444444', zerolinecolor: '#000', gridcolor: '#eee'}};

      // Handle the case when the metric data is empty, we would show
      // graph with No Data annotation.
      if (!isNonEmptyArray(metric.data)) {
        metric.layout["annotations"] = [{
          visible: true,
          align: "top",
          text: "No Data",
          font: {
            color: "#44518b",
            family: "Open Sans",
            size: 14,
          },
          showarrow: false,
          x: 1,
          y: 1
        }];
        metric.layout.margin = {
          l: 0,
          r: 0,
          b: 0,
          t: 0,
        };
        metric.layout.xaxis = {range: [0, 2], color: '#444444', linecolor: '#eee'};
        metric.layout.yaxis = {range: [0, 2], color: '#444444', linecolor: '#eee'};
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
    const width = containerWidth - WIDTH_OFFSET+25+Math.round(16000/containerWidth);
    const columnCount = Math.ceil(width / MAX_GRAPH_WIDTH_PX);
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

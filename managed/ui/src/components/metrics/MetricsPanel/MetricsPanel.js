// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import {
  divideYAxisByThousand,
  isNonEmptyArray,
  isNonEmptyObject,
  isNonEmptyString,
  isYAxisGreaterThanThousand,
  removeNullProperties,
  timeFormatXAxis
} from '../../../utils/ObjectUtils';
import './MetricsPanel.scss';
import { METRIC_FONT } from '../MetricsConfig';
import _ from 'lodash';
import moment from 'moment';
import { OverlayTrigger, Tooltip } from 'react-bootstrap';

import prometheusIcon from '../images/prometheus-icon.svg';

const Plotly = require('plotly.js/lib/core');

const WIDTH_OFFSET = 23;
const CONTAINER_PADDING = 60;
const MAX_GRAPH_WIDTH_PX = 600;
const GRAPH_GUTTER_WIDTH_PX = 15;
const MAX_NAME_LENGTH = 15;

export default class MetricsPanel extends Component {
  static propTypes = {
    metric: PropTypes.object.isRequired,
    metricKey: PropTypes.string.isRequired
  };

  plotGraph = () => {
    const { metricKey, metric, currentUser } = this.props;
    if (isNonEmptyObject(metric)) {
      metric.data.forEach((dataItem, i) => {
        dataItem['fullname'] = dataItem['name'];
        // Truncate trace names if they are longer than 15 characters, and append ellipsis
        if (dataItem['name'].length > MAX_NAME_LENGTH) {
          dataItem['name'] = dataItem['name'].substring(0, MAX_NAME_LENGTH) + '...';
        }
        // Only show upto first 8 traces in the legend
        if (i >= 8) {
          dataItem['showlegend'] = false;
        }
      });
      // Remove Null Properties from the layout
      removeNullProperties(metric.layout);
      // Detect if unit is µs and Y axis value is > 1000.
      // if so divide all Y axis values by 1000 and replace unit to ms.
      if (
        isNonEmptyObject(metric.layout.yaxis) &&
        metric.layout.yaxis.ticksuffix === '&nbsp;µs' &&
        isNonEmptyArray(metric.data)
      ) {
        if (isYAxisGreaterThanThousand(metric.data)) {
          metric.data = divideYAxisByThousand(metric.data);
          metric.layout.yaxis.ticksuffix = '&nbsp;ms';
        }
      }
      metric.data = timeFormatXAxis(metric.data, currentUser.data.timezone);

      metric.layout.xaxis.hoverformat = currentUser.data.timezone
        ? '%H:%M:%S, %b %d, %Y ' + moment.tz(currentUser.data.timezone).format('[UTC]ZZ')
        : '%H:%M:%S, %b %d, %Y ' + moment().format('[UTC]ZZ');

      // TODO: send this data from backend.
      let max = 0;
      metric.data.forEach(function (data) {
        data.hovertemplate = '%{data.fullname}: %{y} at %{x} <extra></extra>';
        if (data.y) {
          data.y.forEach(function (y) {
            y = parseFloat(y) * 1.25;
            if (y > max) max = y;
          });
        }
      });

      if (max === 0) max = 1.01;
      metric.layout.autosize = false;
      metric.layout.width =
        this.props.width || this.getGraphWidth(this.props.containerWidth || 1200);
      metric.layout.height = this.props.height || 400;
      metric.layout.showlegend = true;
      metric.layout.hovermode = 'closest';
      metric.layout.margin = {
        l: 45,
        r: 25,
        b: 0,
        t: 70,
        pad: 14
      };
      if (
        isNonEmptyObject(metric.layout.yaxis) &&
        isNonEmptyString(metric.layout.yaxis.ticksuffix)
      ) {
        metric.layout.margin.l = 70;
        metric.layout.yaxis.range = [0, max];
      } else {
        metric.layout.yaxis = { range: [0, max] };
      }
      metric.layout.font = {
        family: METRIC_FONT
      };

      // Give the legend box extra vertical space if there are more than 4 traces
      let legendExtraMargin = 0;
      if (metric.data.length > 4) {
        legendExtraMargin = 0.2;
      }

      metric.layout.legend = {
        orientation: 'h',
        xanchor: 'center',
        yanchor: 'bottom',
        x: 0.5,
        y: -0.5 - legendExtraMargin
      };

      // Handle the case when the metric data is empty, we would show
      // graph with No Data annotation.
      if (!isNonEmptyArray(metric.data)) {
        metric.layout['annotations'] = [
          {
            visible: true,
            align: 'center',
            text: 'No Data',
            showarrow: false,
            x: 1,
            y: 1
          }
        ];
        metric.layout.margin.b = 105;
        metric.layout.xaxis = { range: [0, 2] };
        metric.layout.yaxis = { range: [0, 2] };
      }
      Plotly.newPlot(metricKey, metric.data, metric.layout, { displayModeBar: false });
    }
  };

  componentDidMount() {
    this.plotGraph();
  }

  componentDidUpdate(prevProps) {
    if (
      this.props.containerWidth !== prevProps.containerWidth ||
      this.props.width !== prevProps.width
    ) {
      Plotly.relayout(prevProps.metricKey, {
        width: this.props.width || this.getGraphWidth(this.props.containerWidth)
      });
    } else {
      // Comparing deep comparison of x-axis and y-axis arrays
      // to avoid re-plotting graph if equal
      const prevData = prevProps.metric.data;
      const currData = this.props.metric.data;
      if (prevData && currData && !_.isEqual(prevData, currData)) {
        // Re-plot graph
        this.plotGraph();
      }
    }
  }

  getGraphWidth(containerWidth) {
    const width = containerWidth - CONTAINER_PADDING - WIDTH_OFFSET;
    const columnCount = Math.ceil(width / MAX_GRAPH_WIDTH_PX);
    return Math.floor(width / columnCount) - GRAPH_GUTTER_WIDTH_PX;
  }

  render() {
    const { prometheusQueryEnabled } = this.props;
    const tooltip = (
      <Tooltip id="tooltip" className="prometheus-link-tooltip">
        Query in Prometheus
      </Tooltip>
    );
    return (
      <div id={this.props.metricKey} className="metrics-panel">
        {prometheusQueryEnabled ? (
          <OverlayTrigger placement="top" overlay={tooltip}>
            <a
              target="_blank"
              rel="noopener noreferrer"
              className="prometheus-link"
              href={this.props.metric.directURL}
            >
              <img
                className="prometheus-link-icon"
                alt="Prometheus"
                src={prometheusIcon}
                width="25"
              />
            </a>
          </OverlayTrigger>
        ) : null}
        <div />
      </div>
    );
  }
}

// Copyright (c) YugaByte, Inc.
// TODO: Entire file needs to be removed once Top K metrics is tested and integrated fully
import React, { Component } from 'react';
import PropTypes from 'prop-types';
import _ from 'lodash';
import moment from 'moment-timezone';
import { OverlayTrigger, Tooltip } from 'react-bootstrap';

import {
  divideYAxisByThousand,
  isNonEmptyArray,
  isNonEmptyObject,
  isNonEmptyString,
  isYAxisGreaterThanThousand,
  removeNullProperties,
  timeFormatXAxis
} from '../../../utils/ObjectUtils';
import { METRIC_FONT } from '../MetricsConfig';
import prometheusIcon from '../images/prometheus-icon.svg';

import './MetricsPanel.scss';

const Plotly = require('plotly.js/lib/core');

const WIDTH_OFFSET = 23;
const CONTAINER_PADDING = 60;
const MAX_GRAPH_WIDTH_PX = 600;
const GRAPH_GUTTER_WIDTH_PX = 15;
const MAX_NAME_LENGTH = 15;

const DEFAULT_HEIGHT = 450;
const DEFAULT_CONTAINER_WIDTH = 1200;

export default class MetricsPanelOld extends Component {
  static propTypes = {
    metric: PropTypes.object.isRequired,
    metricKey: PropTypes.string.isRequired
  };

  plotGraph = () => {
    const { metricKey, metric, currentUser, shouldAbbreviateTraceName = true } = this.props;
    if (isNonEmptyObject(metric)) {
      const layoutHeight = this.props.height || DEFAULT_HEIGHT;
      const layoutWidth =
        this.props.width ||
        this.getGraphWidth(this.props.containerWidth || DEFAULT_CONTAINER_WIDTH);
      metric.data.forEach((dataItem, i) => {
        if (dataItem['instanceName'] && dataItem['name'] !== dataItem['instanceName']) {
          dataItem['fullname'] = dataItem['name'] + ' (' + dataItem['instanceName'] + ')';
        } else {
          dataItem['fullname'] = dataItem['name'];
        }

        // To avoid the legend overlapping plot, we allow the option to abbreviate trace names if:
        // - received truthy shouldAbbreviateTraceName AND
        // - trace name longer than the max name legnth
        if (shouldAbbreviateTraceName && dataItem['name'].length > MAX_NAME_LENGTH) {
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
      metric.layout.width = layoutWidth;
      metric.layout.height = layoutHeight;
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

      metric.layout.legend = {
        orientation: 'h',
        xanchor: 'center',
        yanchor: 'top',
        x: 0.5,
        y: -0.3
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
        Metric graph in Prometheus
      </Tooltip>
    );
    const getMetricsUrl = (internalUrl, metricsLinkUseBrowserFqdn) => {
      if (!metricsLinkUseBrowserFqdn) {
        return internalUrl;
      }
      const url = new URL(internalUrl);
      url.hostname = window.location.hostname;
      return url.href;
    };

    return (
      <div id={this.props.metricKey} className="metrics-panel">
        {prometheusQueryEnabled && isNonEmptyArray(this.props.metric?.directURLs) ? (
          <OverlayTrigger placement="top" overlay={tooltip}>
            <a
              target="_blank"
              rel="noopener noreferrer"
              className="prometheus-link"
              href={getMetricsUrl(this.props.metric.directURLs[0],
               this.props.metric.metricsLinkUseBrowserFqdn)}
            >
              <img
                className="prometheus-link-icon"
                alt="Metric graph in Prometheus"
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

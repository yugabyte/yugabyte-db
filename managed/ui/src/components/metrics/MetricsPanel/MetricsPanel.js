// Copyright (c) YugaByte, Inc.
import React, { Component } from 'react';
import { Button, OverlayTrigger, Tooltip, DropdownButton, MenuItem } from 'react-bootstrap';
import PropTypes from 'prop-types';
import _ from 'lodash';
import moment from 'moment-timezone';
import clsx from 'clsx';

import { MetricConsts, MetricMeasure, MetricTypes } from '../../metrics/constants';
import { METRIC_FONT } from '../MetricsConfig';
import {
  divideYAxisByThousand,
  isNonEmptyArray,
  isNonEmptyObject,
  isNonEmptyString,
  isYAxisGreaterThanThousand,
  removeNullProperties,
  timeFormatXAxis
} from '../../../utils/ObjectUtils';
import prometheusIcon from '../../../redesign/assets/prometheus-icon.svg';

import './MetricsPanel.scss';

const Plotly = require('plotly.js/lib/core');

const MIN_OUTLIER_BUTTONS_WIDTH = 290;
const WIDTH_OFFSET = 23;
const CONTAINER_PADDING = 60;
const MAX_GRAPH_WIDTH_PX = 600;
const GRAPH_GUTTER_WIDTH_PX = 0;
const MAX_NAME_LENGTH = 15;

const DEFAULT_HEIGHT = 400;
const DEFAULT_CONTAINER_WIDTH = 1200;

export default class MetricsPanel extends Component {
  constructor(props) {
    super(props);
    this.outlierButtonsRef = React.createRef();
    this.state = {
      outlierButtonsWidth: null,
      focusedButton: null,
      isItemInDropdown: false
    };
  }

  static propTypes = {
    metric: PropTypes.object.isRequired,
    metricKey: PropTypes.string.isRequired
  };

  plotGraph = (metricOperation = null) => {
    const {
      metricKey,
      currentUser,
      shouldAbbreviateTraceName = true,
      metricMeasure,
      operations = [],
      metricType,
      isMetricLoading = false,
      printMode = false
    } = this.props;
    const newMetricKey = printMode ? `${metricKey}-printMode` : metricKey;
    const metric =
      metricMeasure === MetricMeasure.OUTLIER || metricType === MetricTypes.OUTLIER_TABLES
        ? _.cloneDeep(this.props.metric)
        : this.props.metric;
    if (isNonEmptyObject(metric)) {
      const layoutHeight = this.props.height || DEFAULT_HEIGHT;
      const layoutWidth =
        this.props.width ||
        this.getGraphWidth(this.props.containerWidth || DEFAULT_CONTAINER_WIDTH);
      if (metricOperation || operations.length) {
        const matchingMetricOperation = metricOperation
          ? metricOperation
          : this.state.focusedButton ?? operations[0];
        metric.data = metric.data.filter((dataItem) => dataItem.name === matchingMetricOperation);
      }
      metric.data.forEach((dataItem, i) => {
        if (dataItem['instanceName'] && dataItem['name'] !== dataItem['instanceName']) {
          dataItem['fullname'] = dataItem['name'] + ' (' + dataItem['instanceName'] + ')';
          if (metricMeasure === MetricMeasure.OUTLIER) {
            dataItem['fullname'] = dataItem['instanceName'];
          }
          // If the metrics API returns data without instanceName field in case of
          // outliers, it belongs to cluster average data
        } else if (metricMeasure === MetricMeasure.OUTLIER && !dataItem['instanceName']) {
          dataItem['fullname'] = MetricConsts.NODE_AVERAGE;
        } else if (metricType === MetricTypes.OUTLIER_TABLES) {
          dataItem['fullname'] = dataItem['tableName'];
        } else {
          dataItem['fullname'] = dataItem['name'];
        }

        // To avoid the legend overlapping plot, we allow the option to abbreviate trace names if:
        // - received truthy shouldAbbreviateTraceName AND
        // - trace name longer than the max name legnth
        const shouldAbbreviate =
          shouldAbbreviateTraceName && dataItem['name'].length > MAX_NAME_LENGTH;
        if (shouldAbbreviate && !metricMeasure === MetricMeasure.OUTLIER) {
          dataItem['name'] = dataItem['name'].substring(0, MAX_NAME_LENGTH) + '...';
          // Legend name from outlier should be based on instance name in case of outliers
        } else if (metricMeasure === MetricMeasure.OUTLIER) {
          dataItem['name'] = dataItem['instanceName']
            ? dataItem['instanceName'] +
              (this.state.isItemInDropdown ? ' (' + dataItem['name'] + ')' : '')
            : MetricConsts.NODE_AVERAGE;
        } else if (metricType === MetricTypes.OUTLIER_TABLES) {
          dataItem['name'] = dataItem['namespaceName']
            ? `${dataItem['namespaceName']}.${dataItem['tableName']}`
            : dataItem['tableName'];
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
        if (metricType === MetricTypes.OUTLIER_TABLES && data?.namespaceName) {
          data.hovertemplate =
            '%{data.namespaceName}.%{data.fullname}: %{y} at %{x} <extra></extra>';
        } else {
          data.hovertemplate = '%{data.fullname}: %{y} at %{x} <extra></extra>';
        }
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
      metric.layout.title = {
        text: metric?.layout?.title?.text ?? metric.layout.title,
        x: 0.05,
        y: 2.2,
        xref: 'container',
        yref: 'container',
        font: {
          family: 'Inter',
          size: 15,
          color: '#0B1117'
        }
      };
      metric.layout.hovermode = 'closest';
      metric.layout.margin = {
        l: 45,
        r: 25,
        b: 0,
        t: 70,
        pad: 4
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
            text: isMetricLoading ? 'Loading metric...' : 'No Data',
            showarrow: false,
            x: 1,
            y: 1
          }
        ];
        metric.layout.margin.b = 105;
        metric.layout.xaxis = { range: [0, 2] };
        metric.layout.yaxis = { range: [0, 2] };
      }
      Plotly.newPlot(newMetricKey, metric.data, metric.layout, { displayModeBar: false });
    }
  };

  componentDidMount() {
    this.plotGraph();
    const outlierButtonsWidth = this.outlierButtonsRef.current?.offsetWidth;
    this.setState({
      outlierButtonsWidth: outlierButtonsWidth
    });
  }

  componentDidUpdate(prevProps) {
    // Enables user to view granular data based on selected time range within the graph
    if (this.props.isGranularMetricsEnabled) {
      const metricKeyContainer = document.getElementById(prevProps.newMetricKey);
      metricKeyContainer?.on('plotly_relayout', function (eventData) {
        const startTime = Math.floor(new Date(eventData['xaxis.range[0]']).getTime() / 1000);
        const endTime = Math.floor(new Date(eventData['xaxis.range[1]']).getTime() / 1000);
        prevProps.updateTimestamp(startTime, endTime);
      });
    }

    if (
      this.props.containerWidth !== prevProps.containerWidth ||
      this.props.width !== prevProps.width
    ) {
      Plotly.relayout(prevProps.newMetricKey, {
        width: this.props.width || this.getGraphWidth(this.props.containerWidth)
      });
    } else {
      // Comparing deep comparison of x-axis and y-axis arrays
      // to avoid re-plotting graph if equal
      const prevData = prevProps.metric.data;
      const currData = this.props.metric.data;
      if (prevData && currData && !_.isEqual(prevData, currData)) {
        // When user is on a specific tab and when they switch from Overall
        // to Outlier, we need to ensure offsetWidth is maintained
        if (!this.state.outlierButtonsWidth) {
          this.setState({
            outlierButtonsWidth: this.outlierButtonsRef.current?.offsetWidth
          });
        }
        // Re-plot graph
        this.plotGraph();
      }
    }
  }

  loadDataByMetricOperation(metricOperation, isItemInDropdown) {
    this.setState(
      {
        focusedButton: metricOperation,
        isItemInDropdown: isItemInDropdown
      },
      () => {
        this.plotGraph(metricOperation);
      }
    );
  }

  getGraphWidth(containerWidth) {
    const width = containerWidth - CONTAINER_PADDING - WIDTH_OFFSET;
    const columnCount = Math.ceil(width / MAX_GRAPH_WIDTH_PX);
    return Math.floor(width / columnCount) - GRAPH_GUTTER_WIDTH_PX;
  }

  getClassButtonName(idx, metricOperationsDisplayedLength, metricOperationsDropdownLength) {
    let className = 'outlier-chart-button';
    if (this.props.operations?.length === 1) {
      className = 'outlier-chart-button__only';
    } else if (idx === 0) {
      className = 'outlier-chart-button__first';
    } else if (idx === metricOperationsDisplayedLength - 1 && metricOperationsDropdownLength >= 1) {
      className = 'outlier-chart-button__penultimate';
    } else if (idx === metricOperationsDisplayedLength - 1 && !metricOperationsDropdownLength) {
      className = 'outlier-chart-button__last';
    }
    return className;
  }

  render() {
    const { prometheusQueryEnabled, operations, metricMeasure, metricType, className } = this.props;
    let showDropdown = false;
    let numButtonsInDropdown = 0;
    let metricOperationsDisplayed = operations;
    let metricOperationsDropdown = [];
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

    // Calculate how many buttons should be in dropdown
    if (this.state.outlierButtonsWidth > MIN_OUTLIER_BUTTONS_WIDTH) {
      numButtonsInDropdown = 1;
      if (operations.length === 3) {
        numButtonsInDropdown = 2;
      } else if (operations.length > 3 && operations.length < 6) {
        numButtonsInDropdown = 3;
      } else if (operations.length >= 6) {
        numButtonsInDropdown = 4;
      }
      showDropdown = true;
      metricOperationsDisplayed = operations?.slice(0, operations.length - numButtonsInDropdown);
      metricOperationsDropdown = operations?.slice(operations.length - numButtonsInDropdown);
    }
    const focusedButton = this.state.focusedButton ? this.state.focusedButton : operations?.[0];
    const newMetricKey = this.props.printMode
      ? `${this.props.metricKey}-printMode`
      : this.props.metricKey;
    return (
      <div id={newMetricKey} className={clsx(className, 'metrics-panel')}>
        <span ref={this.outlierButtonsRef} className="outlier-buttons-container">
          {(metricMeasure === MetricMeasure.OUTLIER || metricType === MetricTypes.OUTLIER_TABLES) &&
            operations.length > 0 &&
            metricOperationsDisplayed.map((operation, idx) => {
              return (
                <Button
                  className={this.getClassButtonName(
                    idx,
                    metricOperationsDisplayed.length,
                    metricOperationsDropdown.length
                  )}
                  key={idx}
                  active={operation === focusedButton}
                  onClick={() => this.loadDataByMetricOperation(operation, false)}
                >
                  {operation}
                </Button>
              );
            })}
          {showDropdown && metricOperationsDropdown.length >= 1 && (
            <DropdownButton
              id="outlier-dropdown"
              className="btn btn-default outlier-dropdown"
              pullRight
              title="..."
              noCaret
            >
              {metricOperationsDropdown.map((operation, idx) => {
                return (
                  <MenuItem
                    // className='outlier-button'
                    key={idx}
                    active={operation === focusedButton}
                    onClick={() => this.loadDataByMetricOperation(operation, true)}
                  >
                    {operation}
                  </MenuItem>
                );
              })}
            </DropdownButton>
          )}
        </span>
        {prometheusQueryEnabled && isNonEmptyArray(this.props.metric?.directURLs) ? (
          <OverlayTrigger placement="top" overlay={tooltip}>
            <a
              target="_blank"
              rel="noopener noreferrer"
              className="prometheus-link"
              href={getMetricsUrl(
                this.props.metric.directURLs[0],
                this.props.metric.metricsLinkUseBrowserFqdn
              )}
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

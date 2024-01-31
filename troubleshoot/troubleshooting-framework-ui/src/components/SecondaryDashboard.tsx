import { useEffect, useRef, useState } from 'react';
import { usePrevious } from 'react-use';
import clsx from 'clsx';
import _ from 'lodash';
import { Tooltip, Box, makeStyles, MenuItem } from '@material-ui/core';
import moment from 'moment-timezone';
import { YBButton } from '@yugabytedb/ui-components';
import { YBSelect } from '../common/YBSelect';
import { Anomaly, AppName, MetricMeasure } from '../helpers/dtos';
import {
  isNonEmptyArray,
  isNonEmptyObject,
  removeNullProperties,
  isYAxisGreaterThanThousand,
  divideYAxisByThousand,
  timeFormatXAxis,
  isNonEmptyString
} from '../helpers/ObjectUtils';
import { METRIC_FONT, MetricConsts } from '../helpers/constants';

import PrometheusIcon from '../assets/prometheus-icon.svg';

const Plotly = require('plotly.js/lib/index-basic.js');

interface SecondaryDashboardProps {
  anomalyData: Anomaly | undefined;
  metricData: any;
  appName: AppName;
  metricKey: string;
  containerWidth: number | null;
  prometheusQueryEnabled: boolean;
  metricMeasure: string;
  operations: string[];
  height?: number;
  width?: number;
  shouldAbbreviateTraceName: boolean;
  isMetricLoading: boolean;
  timezone?: string;
  className?: any;
}

const MIN_OUTLIER_BUTTONS_WIDTH = 250;
const WIDTH_OFFSET = 23;
const CONTAINER_PADDING = 60;
const MAX_GRAPH_WIDTH_PX = 1600;
const GRAPH_GUTTER_WIDTH_PX = 0;
const MAX_NAME_LENGTH = 15;

const DEFAULT_HEIGHT = 400;
const DEFAULT_CONTAINER_WIDTH = 1600;

const DEFAULT_CONTAINER_WIDTH_YBM = 1400;
const MAX_GRAPH_WIDTH_PX_YBM = 1400;

const useStyles = makeStyles((theme) => ({
  outlierButton: {
    borderRadius: 0
  },
  outlierOnlyButton: { borderRadius: '8px' },
  outlierFirstButton: { borderRadius: '8px 0 0 8px' },
  outlierPenultimateButton: { borderRadius: 0 },
  outlierLastButton: { borderRadius: '0 8px 8px 0' },
  metricPanel: {
    padding: '1px',
    backgroundColor: '#dedee0',
    margin: '5px 7.5px',
    position: 'relative',
    overflow: 'hidden',
    textOverflow: 'ellipsis',
    whiteSpace: 'nowrap',
    maxWidth: 'fit-content'
  },
  overrideMuiButton: {
    color: '#2b59c3 !important',
    background: 'linear-gradient(0deg, rgba(43, 89, 195, 0.1), rgba(43, 89, 195, 0.1)), #FFFFFF'
  },
  overideMuiSelect: {
    '& .MuiInput-input': {
      color: '#2b59c3 !important',
      background: 'linear-gradient(0deg, rgba(43, 89, 195, 0.1), rgba(43, 89, 195, 0.1)), #FFFFFF'
    },
    '& .MuiInput-root': {
      color: '#2b59c3 !important',
      background: 'linear-gradient(0deg, rgba(43, 89, 195, 0.1), rgba(43, 89, 195, 0.1)), #FFFFFF'
    }
  },
  overrideDefaultMuiSelect: {
    '& .MuiInput-root': {
      height: '32px',
      borderRadius: '0 8px 8px 0'
    }
  },
  menuItem: {
    fontWeight: 300,
    fontSize: '14px',
    padding: '15px 20px',
    height: '52px'
  },
  outlierButtonContaimer: {
    position: 'absolute',
    top: '20px',
    right: '50px',
    display: 'flex',
    flex: 1,
    flexDirection: 'row'
  },
  prometheusIcon: {
    position: 'absolute',
    top: '15px',
    right: '15px',
    marginLeft: '17px',
    marginTop: '5px'
  },
  prometheusLinkTooltip: {
    fontSize: '14px',
    padding: '8px 14px',
    zIndex: 10000
  }
}));

export const SecondaryDashboard = ({
  metricData,
  appName,
  metricKey,
  containerWidth,
  prometheusQueryEnabled,
  metricMeasure,
  operations,
  anomalyData,
  height,
  width,
  shouldAbbreviateTraceName,
  isMetricLoading,
  className,
  timezone
}: SecondaryDashboardProps) => {
  const classes = useStyles();
  let showDropdown = false;
  let numButtonsInDropdown = 0;
  let metricOperationsDisplayed = operations;
  let metricOperationsDropdown: any = [];

  const anomalyStartTime = anomalyData?.startTime;
  const anomalyEndTime = anomalyData?.endTime;
  const utcStartTimeStamp = moment.utc(anomalyStartTime).valueOf();
  const utcEndTimeStamp = anomalyEndTime ? moment.utc(anomalyEndTime).valueOf() : null;
  const [outlierButtonsWidth, setOutlierButtonsWidth] = useState<number | null>(null);
  const [focusedButton, setFocusedButton] = useState<any>(null);
  const [itemInDropdown, setItemInDropdown] = useState<boolean>(false);
  const outlierButtonsRef: any = useRef(null);
  const [active, setActive] = useState<boolean>(false);
  const previousMetricData = usePrevious(metricData);

  const getGraphWidth = (containerWidth: number) => {
    const maxGraphWidth = appName === AppName.YBA ? MAX_GRAPH_WIDTH_PX : MAX_GRAPH_WIDTH_PX_YBM;
    const width = containerWidth - CONTAINER_PADDING - WIDTH_OFFSET;
    const columnCount = Math.ceil(width / maxGraphWidth);
    return Math.floor(width / columnCount) - GRAPH_GUTTER_WIDTH_PX;
  };

  const plotGraph = (metricOperation: string | null) => {
    const metric = _.cloneDeep(metricData);
    if (isNonEmptyObject(metric)) {
      const layoutHeight = height ?? DEFAULT_HEIGHT;
      const layoutWidth =
        width ??
        getGraphWidth(
          containerWidth ?? appName === AppName.YBA
            ? DEFAULT_CONTAINER_WIDTH
            : DEFAULT_CONTAINER_WIDTH_YBM
        );

      if (operations.length || metricOperation) {
        const matchingMetricOperation = metricOperation
          ? metricOperation
          : focusedButton ?? operations[0];
        metric.data = metric.data.filter(
          (dataItem: any) => dataItem.name === matchingMetricOperation
        );
      }

      metric.data.forEach((dataItem: any, i: number) => {
        if (dataItem['instanceName'] && dataItem['name'] !== dataItem['instanceName']) {
          dataItem['fullname'] = dataItem['name'] + ' (' + dataItem['instanceName'] + ')';
          if (metricMeasure === MetricMeasure.OUTLIER) {
            dataItem['fullname'] = dataItem['instanceName'];
          }
          // If the metrics API returns data without instanceName field in case of
          // outliers, it belongs to cluster average data
        } else if (metricMeasure === MetricMeasure.OUTLIER && !dataItem['instanceName']) {
          dataItem['fullname'] = MetricConsts.NODE_AVERAGE;
        } else if (metricMeasure === MetricMeasure.OUTLIER_TABLES) {
          dataItem['fullname'] = dataItem['tableName'];
        } else {
          dataItem['fullname'] = dataItem['name'];
        }

        // To avoid the legend overlapping plot, we allow the option to abbreviate trace names if:
        // - received truthy shouldAbbreviateTraceName AND
        // - trace name longer than the max name legnth
        const shouldAbbreviate =
          shouldAbbreviateTraceName && dataItem['name'].length > MAX_NAME_LENGTH;
        if (shouldAbbreviate && !(metricMeasure === MetricMeasure.OUTLIER)) {
          dataItem['name'] = dataItem['name'].substring(0, MAX_NAME_LENGTH) + '...';
          // Legend name from outlier should be based on instance name in case of outliers
        } else if (metricMeasure === MetricMeasure.OUTLIER) {
          dataItem['name'] = dataItem['instanceName']
            ? dataItem['instanceName'] + (itemInDropdown ? ' (' + dataItem['name'] + ')' : '')
            : MetricConsts.NODE_AVERAGE;
        } else if (metricMeasure === MetricMeasure.OUTLIER_TABLES) {
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

      metric.data = timeFormatXAxis(metric.data, timezone);

      metric.layout.xaxis.hoverformat = timezone
        ? '%H:%M:%S, %b %d, %Y ' + moment.tz(timezone).format('[UTC]ZZ')
        : '%H:%M:%S, %b %d, %Y ' + moment().format('[UTC]ZZ');

      let max = 0;
      metric.data.forEach((data: any) => {
        if (metricMeasure === MetricMeasure.OUTLIER_TABLES && data?.namespaceName) {
          data.hovertemplate =
            '%{data.namespaceName}.%{data.fullname}: %{y} at %{x} <extra></extra>';
        } else {
          data.hovertemplate = '%{data.fullname}: %{y} at %{x} <extra></extra>';
        }
        if (data.y) {
          data.y.forEach((y: any) => {
            y = parseFloat(y) * 1.25;
            if (y > max) max = y;
          });
        }
      });

      // TOOD: Get screen width using window.screen.availWidth
      if (max === 0) max = 1.01;
      metric.layout.autosize = false;
      metric.layout.width = layoutWidth;
      metric.layout.height = layoutHeight;
      metric.layout.showlegend = true;
      metric.layout.shapes = [
        {
          type: 'line',
          // x-reference is assigned to the x-values
          xref: 'x',
          // y-reference is assigned to the plot paper [0,1]
          yref: 'paper',
          x0: utcStartTimeStamp,
          y0: 0,
          x1: utcStartTimeStamp,
          y1: 1,
          fillcolor: '#d3d3d3',
          opacity: 0.2,
          line: {
            color: 'red',
            width: utcEndTimeStamp ? 2.5 : 1.5,
            dash: 'dot'
          }
        },
        {
          type: 'line',
          xref: 'x',
          yref: 'paper',
          x0: utcEndTimeStamp ?? utcStartTimeStamp,
          y0: 0,
          x1: utcEndTimeStamp ?? utcStartTimeStamp,
          y1: 1,
          fillcolor: '#d3d3d3',
          opacity: 0.2,
          line: {
            color: 'red',
            width: utcEndTimeStamp ? 2.5 : 1.5,
            dash: 'dot'
          }
        }
      ];

      metric.layout.title = {
        text: metric.layout.title,
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
      Plotly.newPlot(metricKey, metric.data, metric.layout, {
        displayModeBar: false,
        responsive: true
      });
    }
  };

  const loadDataByMetricOperation = (metricOperation: string, itemInDropdown: boolean) => {
    setFocusedButton(metricOperation);
    setItemInDropdown(itemInDropdown);
    plotGraph(metricOperation);
  };

  // Plot graph during mount of the component with metric data
  useEffect(() => {
    plotGraph(null);
    const outlierButtonsWidth: any = outlierButtonsRef.current?.offsetWidth;
    setOutlierButtonsWidth(outlierButtonsWidth);
  }, []);

  useEffect(() => {
    if (containerWidth || width) {
      Plotly.relayout(metricKey, {
        width: width ?? getGraphWidth(containerWidth!)
      });
    }
  }, [containerWidth, width, metricData]);

  useEffect(() => {
    if (previousMetricData && metricData && !_.isEqual(previousMetricData, metricData)) {
      if (!outlierButtonsWidth) {
        setOutlierButtonsWidth(outlierButtonsRef.current?.offsetWidth);
      }

      // Re-plot graph
      plotGraph(null);
    }
  }, [metricData]);

  const getMetricsUrl = (internalUrl: string, metricsLinkUseBrowserFqdn: boolean) => {
    if (!metricsLinkUseBrowserFqdn) {
      return internalUrl;
    }
    const url = new URL(internalUrl);
    url.hostname = window.location.hostname;
    return url.href;
  };

  const getClassButtonName = (
    idx: number,
    metricOperationsDisplayedLength: number,
    metricOperationsDropdownLength: number
  ) => {
    let className = classes.outlierButton;
    if (operations?.length === 1) {
      className = classes.outlierOnlyButton;
    } else if (idx === 0) {
      className = classes.outlierFirstButton;
    } else if (idx === metricOperationsDisplayedLength - 1 && metricOperationsDropdownLength >= 1) {
      className = classes.outlierPenultimateButton;
    } else if (idx === metricOperationsDisplayedLength - 1 && !metricOperationsDropdownLength) {
      className = classes.outlierLastButton;
    }
    return className;
  };

  if (outlierButtonsWidth! > MIN_OUTLIER_BUTTONS_WIDTH) {
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
  const inFocusButton = focusedButton ? focusedButton : operations?.[0];

  const tooltip = (
    <Tooltip
      title={'Metric graph in Prometheus'}
      arrow
      placement="top"
      className={classes.prometheusIcon}
    >
      <img
        alt="Metric graph in Prometheus"
        src={PrometheusIcon}
        width="25"
        onClick={() => {
          const prometheusUrl = getMetricsUrl(
            metricData.directURLs[0],
            metricData.metricsLinkUseBrowserFqdn
          );
          window.open(prometheusUrl);
        }}
      />
    </Tooltip>
  );

  return (
    <Box id={metricKey} className={clsx(className, classes.metricPanel)}>
      <span ref={outlierButtonsRef} className={classes.outlierButtonContaimer}>
        {(metricMeasure === MetricMeasure.OUTLIER ||
          metricMeasure === MetricMeasure.OUTLIER_TABLES) &&
          operations.length > 0 &&
          metricOperationsDisplayed.map((operation, idx) => {
            const isActive = operation === inFocusButton;
            return (
              <YBButton
                className={clsx(
                  getClassButtonName(
                    idx,
                    metricOperationsDisplayed.length,
                    metricOperationsDropdown.length
                  ),
                  isActive && classes.overrideMuiButton
                )}
                key={idx}
                variant="secondary"
                onClick={() => {
                  setActive(false);
                  loadDataByMetricOperation(operation, false);
                }}
              >
                {operation}
              </YBButton>
            );
          })}
        {showDropdown && metricOperationsDropdown.length >= 1 && (
          <YBSelect
            className={clsx(active && classes.overideMuiSelect, classes.overrideDefaultMuiSelect)}
            // inputProps={{ IconComponent: () => null }}
          >
            {!active && <MenuItem>{'...'}</MenuItem>}
            {metricOperationsDropdown?.map((operation: string, idx: number) => {
              return (
                <MenuItem
                  className={clsx(
                    operation === inFocusButton && classes.overideMuiSelect,
                    classes.menuItem
                  )}
                  key={idx}
                  value={operation}
                  // active={operation === inFocusButton}
                  onClick={() => {
                    setActive(true);
                    loadDataByMetricOperation(operation, true);
                  }}
                >
                  {operation}
                </MenuItem>
              );
            })}
          </YBSelect>
        )}
      </span>
      {prometheusQueryEnabled && isNonEmptyArray(metricData?.directURLs) ? <>{tooltip}</> : null}
      <div />
    </Box>
  );
};

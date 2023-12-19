import { useCallback, useState } from 'react';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import moment from 'moment';
import { Box, makeStyles, useTheme } from '@material-ui/core';
import { Dropdown, MenuItem } from 'react-bootstrap';
import {
  CartesianGrid,
  Legend,
  Line,
  LineChart,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis
} from 'recharts';
import { ToggleButton, ToggleButtonGroup } from '@material-ui/lab';

import { api, metricQueryKey, universeQueryKey } from '../../../redesign/helpers/api';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import {
  DEFAULT_METRIC_TIME_RANGE_OPTION,
  MetricName,
  METRIC_TIME_RANGE_OPTIONS,
  PollingIntervalMs,
  TimeRangeType
} from '../constants';
import {
  adaptMetricDataForRecharts,
  formatUuidFromXCluster,
  getMetricTimeRange
} from '../ReplicationUtils';
import { CustomDatePicker } from '../../metrics/CustomDatePicker/CustomDatePicker';
import { formatDatetime, YBTimeFormats } from '../../../redesign/helpers/DateUtils';
import { CHART_RESIZE_DEBOUNCE } from '../../../redesign/helpers/constants';
import { YBInput } from '../../../redesign/components';
import { assertUnreachableCase } from '../../../utils/errorHandlingUtils';
import { YBMetricGraphTitle } from '../../../redesign/components/YBMetricGraphTitle/YBMetricGraphTitle';

import { MetricTimeRangeOption } from '../XClusterTypes';
import { XClusterConfig } from '../dtos';
import {
  MetricSettings,
  MetricsQueryParams,
  MetricsQueryResponse,
  MetricTrace
} from '../../../redesign/helpers/dtos';
import { NodeAggregation, SplitMode, SplitType } from '../../metrics/dtos';

interface ConfigReplicationLagGraphProps {
  xClusterConfig: XClusterConfig;
}

const useStyles = makeStyles(() => ({
  numericInput: {
    width: '60px'
  }
}));

const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.metricsPanel';

export const XClusterMetrics = ({ xClusterConfig }: ConfigReplicationLagGraphProps) => {
  const [selectedTimeRangeOption, setSelectedTimeRangeOption] = useState<MetricTimeRangeOption>(
    DEFAULT_METRIC_TIME_RANGE_OPTION
  );
  const [customStartMoment, setCustomStartMoment] = useState(
    getMetricTimeRange(DEFAULT_METRIC_TIME_RANGE_OPTION).startMoment
  );
  const [customEndMoment, setCustomEndMoment] = useState(
    getMetricTimeRange(DEFAULT_METRIC_TIME_RANGE_OPTION).endMoment
  );
  const [metricsNodeAggregation, setMetricsNodeAggregation] = useState<NodeAggregation>(
    NodeAggregation.MAX
  );
  const [metricsSplitType, setMetricsSplitType] = useState<SplitType>(SplitType.NONE);
  const [metricsSplitMode, setMetricsSplitMode] = useState<SplitMode>(SplitMode.NONE);
  const [metricsSplitCount, setMetricsSplitCount] = useState<number>(1);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const theme = useTheme();
  const classes = useStyles();

  const sourceUniverseQuery = useQuery(
    universeQueryKey.detail(xClusterConfig.sourceUniverseUUID),
    () => api.fetchUniverse(xClusterConfig.sourceUniverseUUID)
  );

  const targetUniverseQuery = useQuery(
    universeQueryKey.detail(xClusterConfig.targetUniverseUUID),
    () => api.fetchUniverse(xClusterConfig.targetUniverseUUID)
  );

  const targetUniverseNamespaceQuery = useQuery(
    universeQueryKey.namespaces(xClusterConfig.targetUniverseUUID),
    () => api.fetchUniverseNamespaces(xClusterConfig.targetUniverseUUID)
  );

  const isCustomTimeRange = selectedTimeRangeOption.type === TimeRangeType.CUSTOM;
  const metricTimeRange = isCustomTimeRange
    ? { startMoment: customStartMoment, endMoment: customEndMoment }
    : getMetricTimeRange(selectedTimeRangeOption);
  // At the moment, we don't support a custom time range which uses the 'current time' as the end time.
  // Thus, all custom time ranges are fixed.
  const isFixedTimeRange = isCustomTimeRange;
  const replicationLagMetricSettings = {
    metric: MetricName.ASYNC_REPLICATION_SENT_LAG,
    nodeAggregation: metricsNodeAggregation,
    splitType: metricsSplitType,
    splitMode: metricsSplitMode,
    splitCount: metricsSplitCount
  };
  const replciationLagMetricRequestParams: MetricsQueryParams = {
    metricsWithSettings: [replicationLagMetricSettings],
    nodePrefix: sourceUniverseQuery.data?.universeDetails.nodePrefix,
    xClusterConfigUuid: xClusterConfig.uuid,
    start: metricTimeRange.startMoment.format('X'),
    end: metricTimeRange.endMoment.format('X')
  };
  const configReplicationLagMetricQuery = useQuery(
    isFixedTimeRange
      ? metricQueryKey.detail(replciationLagMetricRequestParams)
      : metricQueryKey.latest(
          replciationLagMetricRequestParams,
          selectedTimeRangeOption.value,
          selectedTimeRangeOption.type
        ),
    () => api.fetchMetrics(replciationLagMetricRequestParams),
    {
      enabled: !!sourceUniverseQuery.data,
      // It is unnecessary to refetch metric traces when the interval is fixed as subsequent
      // queries will return the same data.
      staleTime: isFixedTimeRange ? Infinity : 0,
      refetchInterval: isFixedTimeRange ? false : PollingIntervalMs.XCLUSTER_METRICS,
      select: useCallback((metricQueryResponse: MetricsQueryResponse) => {
        const { data: metricTraces = [], ...metricQueryMetadata } =
          metricQueryResponse.async_replication_sent_lag ?? {};
        return {
          ...metricQueryMetadata,
          metricTraces,
          metricData: adaptMetricDataForRecharts(metricTraces, (trace) => getUniqueTraceId(trace))
        };
      }, [])
    }
  );

  const consumerSafeTimeLagMetricSettings = {
    metric: MetricName.CONSUMER_SAFE_TIME_LAG,
    nodeAggregation: metricsNodeAggregation,
    splitType: metricsSplitType,
    splitMode: metricsSplitMode,
    splitCount: metricsSplitCount
  };
  const consumerSafeTimeSkewMetricSettings = {
    metric: MetricName.CONSUMER_SAFE_TIME_SKEW,
    nodeAggregation: metricsNodeAggregation,
    splitType: metricsSplitType,
    splitMode: metricsSplitMode,
    splitCount: metricsSplitCount
  };
  const consumerSafeTimeMetricRequestParams: MetricsQueryParams = {
    metricsWithSettings: [consumerSafeTimeLagMetricSettings, consumerSafeTimeSkewMetricSettings],
    nodePrefix: targetUniverseQuery.data?.universeDetails.nodePrefix,
    start: metricTimeRange.startMoment.format('X'),
    end: metricTimeRange.endMoment.format('X')
  };
  const consumerSafeTimeMetricsQuery = useQuery(
    isFixedTimeRange
      ? metricQueryKey.detail(consumerSafeTimeMetricRequestParams)
      : metricQueryKey.latest(
          consumerSafeTimeMetricRequestParams,
          selectedTimeRangeOption.value,
          selectedTimeRangeOption.type
        ),
    () => api.fetchMetrics(consumerSafeTimeMetricRequestParams),
    {
      enabled: !!targetUniverseQuery.data,
      // It is unnecessary to refetch metric traces when the interval is fixed as subsequent
      // queries will return the same data.
      staleTime: isFixedTimeRange ? Infinity : 0,
      refetchInterval: isFixedTimeRange ? false : PollingIntervalMs.XCLUSTER_METRICS,
      select: useCallback((metricQueryResponse: MetricsQueryResponse) => {
        const {
          data: consumerSafeTimeLagMetricTraces = [],
          ...consumerSafeTimeLagMetricQueryMetadata
        } = metricQueryResponse.consumer_safe_time_lag ?? {};
        const {
          data: consumerSafeTimeSkewMetricTraces = [],
          ...consumerSafeTimeSkewMetricQueryMetadata
        } = metricQueryResponse.consumer_safe_time_skew ?? {};
        return {
          [MetricName.CONSUMER_SAFE_TIME_LAG]: {
            ...consumerSafeTimeLagMetricQueryMetadata,
            metricTraces: consumerSafeTimeLagMetricTraces,
            metricData: adaptMetricDataForRecharts(consumerSafeTimeLagMetricTraces, (trace) =>
              getUniqueTraceId(trace)
            )
          },
          [MetricName.CONSUMER_SAFE_TIME_SKEW]: {
            ...consumerSafeTimeSkewMetricQueryMetadata,
            metricTraces: consumerSafeTimeSkewMetricTraces,
            metricData: adaptMetricDataForRecharts(consumerSafeTimeSkewMetricTraces, (trace) =>
              getUniqueTraceId(trace)
            )
          }
        };
      }, [])
    }
  );

  if (sourceUniverseQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('failedToFetchSourceUniverse', {
          keyPrefix: 'queryError.error',
          universeUuid: xClusterConfig.sourceUniverseUUID
        })}
      />
    );
  }
  if (targetUniverseQuery.isError || targetUniverseNamespaceQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('failedToFetchTargetUniverse', {
          keyPrefix: 'queryError.error',
          universeUuid: xClusterConfig.sourceUniverseUUID
        })}
      />
    );
  }
  // IMPROVEMENT: Isolate the error message to the graph for which metrics failed to load.
  //              Draw a placeholder graph showing no data and an error message.
  //              Tracking with PLAT-11663
  if (configReplicationLagMetricQuery.isError || consumerSafeTimeMetricsQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('failedToFetchMetrics', {
          keyPrefix: 'queryError'
        })}
      />
    );
  }

  if (
    sourceUniverseQuery.isLoading ||
    sourceUniverseQuery.isIdle ||
    targetUniverseQuery.isLoading ||
    targetUniverseQuery.isIdle
  ) {
    return <YBLoading />;
  }

  const handleMetricNodeAggregationChange = (
    _event: React.MouseEvent<HTMLElement>,
    metricsNodeAggregation: NodeAggregation
  ) => {
    setMetricsNodeAggregation(metricsNodeAggregation);
  };
  const handleMetricsSplitTypeChange = (
    _event: React.MouseEvent<HTMLElement>,
    metricsSplitType: SplitType
  ) => {
    setMetricsSplitType(metricsSplitType);
  };
  const handleMetricsSplitModeChange = (
    _event: React.MouseEvent<HTMLElement>,
    metricsSplitMode: SplitMode
  ) => {
    setMetricsSplitMode(metricsSplitMode);
  };
  const handleTimeRangeChange = (eventKey: any) => {
    const selectedOption = METRIC_TIME_RANGE_OPTIONS[eventKey];
    if (selectedOption.type !== 'divider') {
      setSelectedTimeRangeOption(selectedOption);
    }
  };

  const menuItems = METRIC_TIME_RANGE_OPTIONS.map((option, idx) => {
    if (option.type === 'divider') {
      return <MenuItem divider key={`${idx}_divider`} />;
    }

    return (
      <MenuItem
        onSelect={handleTimeRangeChange}
        key={`${idx}_${option.label}`}
        eventKey={idx}
        active={option.label === selectedTimeRangeOption?.label}
      >
        {option.label}
      </MenuItem>
    );
  });

  const namespaceUuidToNamespace = targetUniverseNamespaceQuery.data
    ? Object.fromEntries(
        targetUniverseNamespaceQuery.data.map((namespace) => [
          namespace.namespaceUUID,
          namespace.name
        ])
      )
    : {};
  const traceStrokes = Object.values(theme.palette.chart.stroke);
  const configReplicationLagMetrics = configReplicationLagMetricQuery.data ?? {
    metricData: [],
    layout: undefined,
    metricTraces: []
  };
  const consumerSafeTimeMetrics = consumerSafeTimeMetricsQuery.data ?? {
    [MetricName.CONSUMER_SAFE_TIME_LAG]: { metricData: [], layout: undefined, metricTraces: [] },
    [MetricName.CONSUMER_SAFE_TIME_SKEW]: { metricData: [], layout: undefined, metricTraces: [] }
  };
  return (
    <div>
      <Box display="flex">
        <Box display="flex" flexDirection="column" gridGap={theme.spacing(1)}>
          <ToggleButtonGroup
            value={metricsNodeAggregation}
            exclusive
            onChange={handleMetricNodeAggregationChange}
          >
            <ToggleButton value={NodeAggregation.MAX}>
              {t('graphFilter.nodeAggregation.max')}
            </ToggleButton>
            <ToggleButton value={NodeAggregation.MIN}>
              {t('graphFilter.nodeAggregation.min')}
            </ToggleButton>
            <ToggleButton value={NodeAggregation.AVERAGE}>
              {t('graphFilter.nodeAggregation.average')}
            </ToggleButton>
          </ToggleButtonGroup>
          <ToggleButtonGroup
            value={metricsSplitType}
            exclusive
            onChange={handleMetricsSplitTypeChange}
          >
            <ToggleButton value={SplitType.NONE}>{t('graphFilter.splitType.cluster')}</ToggleButton>
            <ToggleButton value={SplitType.NODE}>{t('graphFilter.splitType.node')}</ToggleButton>
            <ToggleButton value={SplitType.NAMESPACE}>
              {t('graphFilter.splitType.namespace')}
            </ToggleButton>
            <ToggleButton value={SplitType.TABLE}>{t('graphFilter.splitType.table')}</ToggleButton>
          </ToggleButtonGroup>
          <Box display="flex" gridGap={theme.spacing(1)}>
            <ToggleButtonGroup
              value={metricsSplitMode}
              exclusive
              onChange={handleMetricsSplitModeChange}
            >
              <ToggleButton value={SplitMode.NONE}>{t('graphFilter.splitMode.all')}</ToggleButton>
              <ToggleButton value={SplitMode.TOP}>{t('graphFilter.splitMode.top')}</ToggleButton>
              <ToggleButton value={SplitMode.BOTTOM}>
                {t('graphFilter.splitMode.bottom')}
              </ToggleButton>
            </ToggleButtonGroup>
            {metricsSplitMode !== SplitMode.NONE && (
              <YBInput
                className={classes.numericInput}
                type="number"
                inputProps={{ min: 1, 'data-testid': 'XClusterMetrics-SplitCountInput' }}
                value={metricsSplitCount}
                onChange={(event) => setMetricsSplitCount(parseInt(event.target.value))}
                inputMode="numeric"
              />
            )}
          </Box>
        </Box>
        <Box marginLeft="auto">
          {selectedTimeRangeOption.type === TimeRangeType.CUSTOM && (
            <CustomDatePicker
              startMoment={customStartMoment}
              endMoment={customEndMoment}
              setStartMoment={(dateString: any) => setCustomStartMoment(moment(dateString))}
              setEndMoment={(dateString: any) => setCustomEndMoment(moment(dateString))}
              handleTimeframeChange={configReplicationLagMetricQuery.refetch}
            />
          )}
          <Dropdown id="LagGraphTimeRangeDropdown" pullRight>
            <Dropdown.Toggle>
              <i className="fa fa-clock-o"></i>&nbsp;
              {selectedTimeRangeOption?.label}
            </Dropdown.Toggle>
            <Dropdown.Menu>{menuItems}</Dropdown.Menu>
          </Dropdown>
        </Box>
      </Box>
      <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)} marginTop={2}>
        <Box
          display="flex"
          flexDirection="column"
          justifyContent="center"
          width="100%"
          height="600px"
        >
          <YBMetricGraphTitle
            title={t('graphTitle.asyncReplicationSentLag')}
            metricsLinkUseBrowserFqdn={configReplicationLagMetrics.metricsLinkUseBrowserFqdn}
            directUrls={configReplicationLagMetrics.directURLs}
          />
          <ResponsiveContainer width="100%" height="100%" debounce={CHART_RESIZE_DEBOUNCE}>
            <LineChart
              data={configReplicationLagMetrics.metricData}
              margin={{
                top: theme.spacing(2),
                bottom: theme.spacing(2),
                left: theme.spacing(2),
                right: theme.spacing(2)
              }}
            >
              <CartesianGrid strokeDasharray="3 3" />
              <XAxis
                dataKey="x"
                tickFormatter={(value) =>
                  formatDatetime(value, YBTimeFormats.YB_TIME_ONLY_TIMESTAMP)
                }
                type="number"
                domain={['dataMin', 'dataMax']}
              />
              <YAxis tickFormatter={(value) => `${value} ms`} />
              <Tooltip
                labelFormatter={(value) => formatDatetime(value)}
                isAnimationActive={false}
              />
              <Legend />
              {configReplicationLagMetrics.metricTraces.map((trace, index) => {
                const timeSeriesKey = getUniqueTraceId(trace);
                const timeSeriesName = getUniqueTraceName(
                  replicationLagMetricSettings,
                  trace,
                  namespaceUuidToNamespace
                );
                return (
                  <Line
                    key={timeSeriesKey}
                    name={timeSeriesName}
                    type="linear"
                    dataKey={timeSeriesKey}
                    stroke={traceStrokes[index]}
                    unit=" ms"
                  />
                );
              })}
            </LineChart>
          </ResponsiveContainer>
        </Box>
        <Box
          display="flex"
          flexDirection="column"
          justifyContent="center"
          width="100%"
          height="600px"
        >
          <YBMetricGraphTitle
            title={t('graphTitle.consumerSafeTimeLag')}
            metricsLinkUseBrowserFqdn={configReplicationLagMetrics.metricsLinkUseBrowserFqdn}
            directUrls={configReplicationLagMetrics.directURLs}
          />
          <ResponsiveContainer width="100%" height="100%" debounce={CHART_RESIZE_DEBOUNCE}>
            <LineChart
              data={consumerSafeTimeMetrics[MetricName.CONSUMER_SAFE_TIME_LAG].metricData}
              margin={{
                top: theme.spacing(2),
                bottom: theme.spacing(2),
                left: theme.spacing(2),
                right: theme.spacing(2)
              }}
            >
              <CartesianGrid strokeDasharray="3 3" />
              <XAxis
                dataKey="x"
                tickFormatter={(value) =>
                  formatDatetime(value, YBTimeFormats.YB_TIME_ONLY_TIMESTAMP)
                }
                type="number"
                domain={['dataMin', 'dataMax']}
              />
              <YAxis tickFormatter={(value) => `${value} ms`} />
              <Tooltip
                labelFormatter={(value) => formatDatetime(value)}
                isAnimationActive={false}
              />
              <Legend />
              {consumerSafeTimeMetrics[MetricName.CONSUMER_SAFE_TIME_LAG].metricTraces.map(
                (trace, index) => {
                  const timeSeriesKey = getUniqueTraceId(trace);
                  const timeSeriesName = getUniqueTraceName(
                    consumerSafeTimeLagMetricSettings,
                    trace,
                    namespaceUuidToNamespace
                  );
                  return (
                    <Line
                      key={timeSeriesKey}
                      name={timeSeriesName}
                      type="linear"
                      dataKey={timeSeriesKey}
                      stroke={traceStrokes[index]}
                      unit=" ms"
                    />
                  );
                }
              )}
            </LineChart>
          </ResponsiveContainer>
        </Box>
        <Box
          display="flex"
          flexDirection="column"
          justifyContent="center"
          width="100%"
          height="600px"
        >
          <YBMetricGraphTitle
            title={t('graphTitle.consumerSafeTimeSkew')}
            metricsLinkUseBrowserFqdn={configReplicationLagMetrics.metricsLinkUseBrowserFqdn}
            directUrls={configReplicationLagMetrics.directURLs}
          />
          <ResponsiveContainer width="100%" height="100%" debounce={CHART_RESIZE_DEBOUNCE}>
            <LineChart
              data={consumerSafeTimeMetrics[MetricName.CONSUMER_SAFE_TIME_SKEW].metricData}
              margin={{
                top: theme.spacing(2),
                bottom: theme.spacing(2),
                left: theme.spacing(2),
                right: theme.spacing(2)
              }}
            >
              <CartesianGrid strokeDasharray="3 3" />
              <XAxis
                dataKey="x"
                tickFormatter={(value) =>
                  formatDatetime(value, YBTimeFormats.YB_TIME_ONLY_TIMESTAMP)
                }
                type="number"
                domain={['dataMin', 'dataMax']}
              />
              <YAxis tickFormatter={(value) => `${value} ms`} />
              <Tooltip
                labelFormatter={(value) => formatDatetime(value)}
                isAnimationActive={false}
              />
              <Legend />
              {consumerSafeTimeMetrics[MetricName.CONSUMER_SAFE_TIME_SKEW].metricTraces.map(
                (trace, index) => {
                  const timeSeriesKey = getUniqueTraceId(trace);
                  const timeSeriesName = getUniqueTraceName(
                    consumerSafeTimeSkewMetricSettings,
                    trace,
                    namespaceUuidToNamespace
                  );
                  return (
                    <Line
                      key={timeSeriesKey}
                      name={timeSeriesName}
                      type="linear"
                      dataKey={timeSeriesKey}
                      stroke={traceStrokes[index]}
                      unit=" ms"
                    />
                  );
                }
              )}
            </LineChart>
          </ResponsiveContainer>
        </Box>
      </Box>
    </div>
  );
};

const getUniqueTraceId = (trace: MetricTrace): string =>
  `${trace.name}.${trace.instanceName}.${trace.namespaceId ?? trace.namespaceName}.${
    trace.tableId ?? trace.tableName
  }`;

const getUniqueTraceName = (
  metricSettings: MetricSettings,
  trace: MetricTrace,
  namespaceUuidToNamespace: { [namespaceUuid: string]: string | undefined }
): string => {
  if (
    metricSettings.splitMode !== SplitMode.NONE &&
    !(
      trace.instanceName ||
      trace.namespaceId ||
      trace.namespaceName ||
      trace.tableId ||
      trace.tableName
    )
  ) {
    return `${trace.name} (Average)`;
  }
  switch (metricSettings.splitType) {
    case undefined:
    case SplitType.NONE:
      return `${trace.name}`;
    case SplitType.NODE:
      return `${trace.name} (${trace.instanceName})`;
    case SplitType.NAMESPACE: {
      const namespaceName =
        trace.namespaceName ??
        namespaceUuidToNamespace[formatUuidFromXCluster(trace.namespaceId ?? '')];
      return namespaceName ? `${trace.name} (${namespaceName})` : trace.name;
    }
    case SplitType.TABLE: {
      const namespaceName =
        trace.namespaceName ??
        namespaceUuidToNamespace[formatUuidFromXCluster(trace.namespaceId ?? '<unknown>')];
      const tableIdentifier = trace.tableName
        ? `${namespaceName}/${trace.tableName}`
        : namespaceName;
      return `${trace.name} (${tableIdentifier})`;
    }
    default:
      return assertUnreachableCase(metricSettings.splitType);
  }
};

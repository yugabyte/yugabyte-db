import { useCallback, useState } from 'react';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import moment from 'moment';
import { Box, Typography, useTheme } from '@material-ui/core';
import { Dropdown, MenuItem } from 'react-bootstrap';
import {
  CartesianGrid,
  Legend,
  Line,
  LineChart,
  ReferenceLine,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis
} from 'recharts';
import { ToggleButton } from '@material-ui/lab';

import {
  alertConfigQueryKey,
  api,
  metricQueryKey,
  universeQueryKey
} from '../../../../redesign/helpers/api';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import {
  AlertName,
  DEFAULT_METRIC_TIME_RANGE_OPTION,
  MetricName,
  METRIC_TIME_RANGE_OPTIONS,
  PollingIntervalMs,
  TimeRangeType
} from '../../constants';
import {
  adaptMetricDataForRecharts,
  formatUuidFromXCluster,
  getMetricTimeRange
} from '../../ReplicationUtils';
import { CustomDatePicker } from '../../../metrics/CustomDatePicker/CustomDatePicker';
import { formatDatetime, YBTimeFormats } from '../../../../redesign/helpers/DateUtils';
import { CHART_RESIZE_DEBOUNCE } from '../../../../redesign/helpers/constants';
import { assertUnreachableCase } from '../../../../utils/errorHandlingUtils';
import { YBMetricGraphTitle } from '../../../../redesign/components/YBMetricGraphTitle/YBMetricGraphTitle';
import { getAlertConfigurations } from '../../../../actions/universe';
import { MetricsFilter } from './MetricsFilter';
import { YBTooltip } from '../../../../redesign/components';

import { MetricTimeRangeOption } from '../../XClusterTypes';
import { XClusterConfig } from '../../dtos';
import {
  MetricSettings,
  MetricsQueryParams,
  MetricsQueryResponse,
  MetricTrace
} from '../../../../redesign/helpers/dtos';
import { NodeAggregation, SplitMode, SplitType } from '../../../metrics/dtos';

interface ConfigReplicationLagGraphProps {
  xClusterConfig: XClusterConfig;
}

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
  const [showAlertThresholdReferenceLine, setShowAlertThresholdReferenceLIne] = useState<boolean>(
    false
  );
  const [replicationLagMetricsNodeAggregation, setReplicationLagMetricsNodeAggregation] = useState<
    NodeAggregation
  >(NodeAggregation.MAX);
  const [replicationLagMetricsSplitType, setReplicationLagMetricsSplitType] = useState<SplitType>(
    SplitType.NONE
  );
  const [replicationLagMetricsSplitMode, setReplicationLagMetricsSplitMode] = useState<SplitMode>(
    SplitMode.NONE
  );
  const [replicationLagMetricsSplitCount, setReplicationLagMetricsSplitCount] = useState<number>(5);

  const [
    consumerSafeTimeLagMetricsNodeAggregation,
    setConsumerSafeTimeLagMetricsNodeAggregation
  ] = useState<NodeAggregation>(NodeAggregation.MAX);
  const [consumerSafeTimeLagMetricsSplitType, setConsumerSafeTimeLagMetricsSplitType] = useState<
    SplitType
  >(SplitType.NONE);
  const [consumerSafeTimeLagMetricsSplitMode, setConsumerSafeTimeLagMetricsSplitMode] = useState<
    SplitMode
  >(SplitMode.NONE);
  const [consumerSafeTimeLagMetricsSplitCount, setConsumerSafeTimeLagMetricsSplitCount] = useState<
    number
  >(1);

  const [
    consumerSafeTimeSkewMetricsNodeAggregation,
    setConsumerSafeTimeSkewMetricsNodeAggregation
  ] = useState<NodeAggregation>(NodeAggregation.MAX);
  const [consumerSafeTimeSkewMetricsSplitType, setConsumerSafeTimeSkewMetricsSplitType] = useState<
    SplitType
  >(SplitType.NONE);
  const [consumerSafeTimeSkewMetricsSplitMode, setConsumerSafeTimeSkewMetricsSplitMode] = useState<
    SplitMode
  >(SplitMode.NONE);
  const [
    consumerSafeTimeSkewMetricsSplitCount,
    setConsumerSafeTimeSkewMetricsSplitCount
  ] = useState<number>(1);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const theme = useTheme();

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

  const alertConfigFilter = {
    name: AlertName.REPLICATION_LAG,
    targetUuid: xClusterConfig.sourceUniverseUUID
  };
  const alertConfigQuery = useQuery<any[]>(alertConfigQueryKey.list(alertConfigFilter), () =>
    getAlertConfigurations(alertConfigFilter)
  );
  const maxAcceptableLag = alertConfigQuery.data?.length
    ? Math.min(
        ...alertConfigQuery.data.map(
          (alertConfig: any): number => alertConfig.thresholds.SEVERE.threshold
        )
      )
    : undefined;
  const isCustomTimeRange = selectedTimeRangeOption.type === TimeRangeType.CUSTOM;
  const metricTimeRange = isCustomTimeRange
    ? { startMoment: customStartMoment, endMoment: customEndMoment }
    : getMetricTimeRange(selectedTimeRangeOption);
  // At the moment, we don't support a custom time range which uses the 'current time' as the end time.
  // Thus, all custom time ranges are fixed.
  const isFixedTimeRange = isCustomTimeRange;
  const replicationLagMetricSettings = {
    metric: MetricName.ASYNC_REPLICATION_SENT_LAG,
    nodeAggregation: replicationLagMetricsNodeAggregation,
    splitType: replicationLagMetricsSplitType,
    splitMode: replicationLagMetricsSplitMode,
    splitCount: replicationLagMetricsSplitCount
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
    nodeAggregation: consumerSafeTimeLagMetricsNodeAggregation,
    splitType: consumerSafeTimeLagMetricsSplitType,
    splitMode: consumerSafeTimeLagMetricsSplitMode,
    splitCount: consumerSafeTimeLagMetricsSplitCount
  };
  const consumerSafeTimeLagMetricRequestParams: MetricsQueryParams = {
    metricsWithSettings: [consumerSafeTimeLagMetricSettings],
    nodePrefix: targetUniverseQuery.data?.universeDetails.nodePrefix,
    start: metricTimeRange.startMoment.format('X'),
    end: metricTimeRange.endMoment.format('X')
  };
  const consumerSafeTimeLagMetricsQuery = useQuery(
    isFixedTimeRange
      ? metricQueryKey.detail(consumerSafeTimeLagMetricRequestParams)
      : metricQueryKey.latest(
          consumerSafeTimeLagMetricRequestParams,
          selectedTimeRangeOption.value,
          selectedTimeRangeOption.type
        ),
    () => api.fetchMetrics(consumerSafeTimeLagMetricRequestParams),
    {
      enabled: !!targetUniverseQuery.data,
      // It is unnecessary to refetch metric traces when the interval is fixed as subsequent
      // queries will return the same data.
      staleTime: isFixedTimeRange ? Infinity : 0,
      refetchInterval: isFixedTimeRange ? false : PollingIntervalMs.XCLUSTER_METRICS,
      select: useCallback((metricQueryResponse: MetricsQueryResponse) => {
        const { data: metricTraces = [], ...metricQueryMetadata } =
          metricQueryResponse.consumer_safe_time_lag ?? {};
        return {
          ...metricQueryMetadata,
          metricTraces,
          metricData: adaptMetricDataForRecharts(metricTraces, (trace) => getUniqueTraceId(trace))
        };
      }, [])
    }
  );

  const consumerSafeTimeSkewMetricSettings = {
    metric: MetricName.CONSUMER_SAFE_TIME_SKEW,
    nodeAggregation: consumerSafeTimeSkewMetricsNodeAggregation,
    splitType: consumerSafeTimeSkewMetricsSplitType,
    splitMode: consumerSafeTimeSkewMetricsSplitMode,
    splitCount: consumerSafeTimeSkewMetricsSplitCount
  };
  const consumerSafeTimeSkewMetricRequestParams: MetricsQueryParams = {
    metricsWithSettings: [consumerSafeTimeSkewMetricSettings],
    nodePrefix: targetUniverseQuery.data?.universeDetails.nodePrefix,
    start: metricTimeRange.startMoment.format('X'),
    end: metricTimeRange.endMoment.format('X')
  };
  const consumerSafeTimeSkewMetricsQuery = useQuery(
    isFixedTimeRange
      ? metricQueryKey.detail(consumerSafeTimeSkewMetricRequestParams)
      : metricQueryKey.latest(
          consumerSafeTimeSkewMetricRequestParams,
          selectedTimeRangeOption.value,
          selectedTimeRangeOption.type
        ),
    () => api.fetchMetrics(consumerSafeTimeSkewMetricRequestParams),
    {
      enabled: !!targetUniverseQuery.data,
      // It is unnecessary to refetch metric traces when the interval is fixed as subsequent
      // queries will return the same data.
      staleTime: isFixedTimeRange ? Infinity : 0,
      refetchInterval: isFixedTimeRange ? false : PollingIntervalMs.XCLUSTER_METRICS,
      select: useCallback((metricQueryResponse: MetricsQueryResponse) => {
        const { data: metricTraces = [], ...metricQueryMetadata } =
          metricQueryResponse.consumer_safe_time_skew ?? {};
        return {
          ...metricQueryMetadata,
          metricTraces,
          metricData: adaptMetricDataForRecharts(metricTraces, (trace) => getUniqueTraceId(trace))
        };
      }, [])
    }
  );
  if (sourceUniverseQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('failedToFetchSourceUniverse', {
          keyPrefix: 'queryError',
          universeUuid: xClusterConfig.sourceUniverseUUID
        })}
      />
    );
  }
  if (targetUniverseQuery.isError || targetUniverseNamespaceQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('failedToFetchTargetUniverse', {
          keyPrefix: 'queryError',
          universeUuid: xClusterConfig.sourceUniverseUUID
        })}
      />
    );
  }
  // IMPROVEMENT: Isolate the error message to the graph for which metrics failed to load.
  //              Draw a placeholder graph showing no data and an error message.
  //              Tracking with PLAT-11663
  if (
    configReplicationLagMetricQuery.isError ||
    consumerSafeTimeLagMetricsQuery.isError ||
    consumerSafeTimeSkewMetricsQuery.isError
  ) {
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

  const handleToggleShowAlertThresholdReferenceLine = () =>
    setShowAlertThresholdReferenceLIne(!showAlertThresholdReferenceLine);

  const getUniqueTraceName = (
    metricSettings: MetricSettings,
    trace: MetricTrace,
    namespaceUuidToNamespace: { [namespaceUuid: string]: string | undefined }
  ): string => {
    const traceName = t(trace.name, { keyPrefix: 'prometheusMetricTrace' });
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
      // If the we're showing top/bottom k and there is a trace with no extra metadata, then
      // we assume this is the average trace.
      return `${traceName} (Average)`;
    }
    switch (metricSettings.splitType) {
      case undefined:
      case SplitType.NONE:
        return traceName;
      case SplitType.NODE:
        return `${traceName} (${trace.instanceName})`;
      case SplitType.NAMESPACE: {
        const namespaceName =
          trace.namespaceName ??
          namespaceUuidToNamespace[formatUuidFromXCluster(trace.namespaceId ?? '')];
        return namespaceName ? `${traceName} (${namespaceName})` : traceName;
      }
      case SplitType.TABLE: {
        const namespaceName =
          trace.namespaceName ??
          namespaceUuidToNamespace[formatUuidFromXCluster(trace.namespaceId ?? '<unknown>')];
        const tableIdentifier = trace.tableName
          ? `${namespaceName}/${trace.tableName}`
          : namespaceName;
        return `${traceName} (${tableIdentifier})`;
      }
      default:
        return assertUnreachableCase(metricSettings.splitType);
    }
  };

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
  const consumerSafeTimeLagMetrics = consumerSafeTimeLagMetricsQuery.data ?? {
    metricData: [],
    layout: undefined,
    metricTraces: []
  };
  const consumerSafeTimeSkewMetrics = consumerSafeTimeSkewMetricsQuery.data ?? {
    metricData: [],
    layout: undefined,
    metricTraces: []
  };

  return (
    <div>
      <Box display="flex">
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
          <Box display="flex" justifyContent="space-between">
            <YBMetricGraphTitle
              title={t('graphTitle.asyncReplicationSentLag')}
              metricsLinkUseBrowserFqdn={configReplicationLagMetrics.metricsLinkUseBrowserFqdn}
              directUrls={configReplicationLagMetrics.directURLs}
            />
            <Box display="flex" gridGap={theme.spacing(2)}>
              <YBTooltip
                title={
                  maxAcceptableLag === undefined
                    ? t('showAlertThresholdReferenceLine.disabledTooltip')
                    : ''
                }
                placement="top"
              >
                <span>
                  <ToggleButton
                    value="check"
                    selected={showAlertThresholdReferenceLine}
                    onChange={handleToggleShowAlertThresholdReferenceLine}
                    disabled={maxAcceptableLag === undefined}
                  >
                    <Box display="flex" gridGap={theme.spacing(0.5)} alignItems="center">
                      <i className="fa fa-bell" aria-hidden="true" />
                      <Typography variant="body2" display="inline" component="span">
                        {t('showAlertThresholdReferenceLine.label')}
                      </Typography>
                    </Box>
                  </ToggleButton>
                </span>
              </YBTooltip>
              <MetricsFilter
                metricsNodeAggregation={replicationLagMetricsNodeAggregation}
                metricsSplitType={replicationLagMetricsSplitType}
                metricsSplitMode={replicationLagMetricsSplitMode}
                metricsSplitCount={replicationLagMetricsSplitCount}
                metricsSplitTypeOptions={[
                  SplitType.NAMESPACE,
                  SplitType.NODE,
                  SplitType.NONE,
                  SplitType.TABLE
                ]}
                setMetricsNodeAggregation={setReplicationLagMetricsNodeAggregation}
                setMetricsSplitType={setReplicationLagMetricsSplitType}
                setMetricsSplitMode={setReplicationLagMetricsSplitMode}
                setMetricsSplitCount={setReplicationLagMetricsSplitCount}
              />
            </Box>
          </Box>
          <ResponsiveContainer width="100%" height="100%" debounce={CHART_RESIZE_DEBOUNCE}>
            <LineChart
              data={configReplicationLagMetrics.metricData}
              margin={{
                top: theme.spacing(3),
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
              <Legend iconType="plainline" />
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
              {maxAcceptableLag !== undefined && showAlertThresholdReferenceLine && (
                <>
                  {/* Line component with no dataKey used to add the reference line in the legend.
                      Recharts doesn't provide an option to add reference lines to the legend directly. */}
                  <Line
                    name={t('lowestReplicationLagAlertThresholdLabel')}
                    stroke="#EF5824"
                    strokeDasharray="4 4"
                  />
                  <ReferenceLine
                    y={maxAcceptableLag}
                    stroke="#EF5824"
                    ifOverflow="extendDomain"
                    strokeDasharray="4 4"
                  />
                </>
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
          <Box display="flex" justifyContent="space-between">
            <YBMetricGraphTitle
              title={t('graphTitle.consumerSafeTimeLag')}
              metricsLinkUseBrowserFqdn={consumerSafeTimeLagMetrics.metricsLinkUseBrowserFqdn}
              directUrls={consumerSafeTimeLagMetrics.directURLs}
            />
            <MetricsFilter
              metricsNodeAggregation={consumerSafeTimeLagMetricsNodeAggregation}
              metricsSplitType={consumerSafeTimeLagMetricsSplitType}
              metricsSplitMode={consumerSafeTimeLagMetricsSplitMode}
              metricsSplitCount={consumerSafeTimeLagMetricsSplitCount}
              metricsSplitTypeOptions={[SplitType.NAMESPACE, SplitType.NONE]}
              setMetricsNodeAggregation={setConsumerSafeTimeLagMetricsNodeAggregation}
              setMetricsSplitType={setConsumerSafeTimeLagMetricsSplitType}
              setMetricsSplitMode={setConsumerSafeTimeLagMetricsSplitMode}
              setMetricsSplitCount={setConsumerSafeTimeLagMetricsSplitCount}
            />
          </Box>
          <ResponsiveContainer width="100%" height="100%" debounce={CHART_RESIZE_DEBOUNCE}>
            <LineChart
              data={consumerSafeTimeLagMetrics.metricData}
              margin={{
                top: theme.spacing(3),
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
              <Legend iconType="plainline" />
              {consumerSafeTimeLagMetrics.metricTraces.map((trace, index) => {
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
          <Box display="flex" justifyContent="space-between">
            <YBMetricGraphTitle
              title={t('graphTitle.consumerSafeTimeSkew')}
              metricsLinkUseBrowserFqdn={consumerSafeTimeSkewMetrics.metricsLinkUseBrowserFqdn}
              directUrls={consumerSafeTimeSkewMetrics.directURLs}
            />
            <MetricsFilter
              metricsNodeAggregation={consumerSafeTimeSkewMetricsNodeAggregation}
              metricsSplitType={consumerSafeTimeSkewMetricsSplitType}
              metricsSplitMode={consumerSafeTimeSkewMetricsSplitMode}
              metricsSplitCount={consumerSafeTimeSkewMetricsSplitCount}
              metricsSplitTypeOptions={[SplitType.NAMESPACE, SplitType.NONE]}
              setMetricsNodeAggregation={setConsumerSafeTimeSkewMetricsNodeAggregation}
              setMetricsSplitType={setConsumerSafeTimeSkewMetricsSplitType}
              setMetricsSplitMode={setConsumerSafeTimeSkewMetricsSplitMode}
              setMetricsSplitCount={setConsumerSafeTimeSkewMetricsSplitCount}
            />
          </Box>
          <ResponsiveContainer width="100%" height="100%" debounce={CHART_RESIZE_DEBOUNCE}>
            <LineChart
              data={consumerSafeTimeSkewMetrics.metricData}
              margin={{
                top: theme.spacing(3),
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
              <Legend iconType="plainline" />
              {consumerSafeTimeSkewMetrics.metricTraces.map((trace, index) => {
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
              })}
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

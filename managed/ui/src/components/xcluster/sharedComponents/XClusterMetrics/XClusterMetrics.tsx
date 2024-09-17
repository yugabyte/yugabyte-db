import { useCallback, useState } from 'react';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import moment from 'moment';
import { Box, Typography, useTheme } from '@material-ui/core';
import { Dropdown, MenuItem } from 'react-bootstrap';
import { ToggleButton } from '@material-ui/lab';
import i18next from 'i18next';

import {
  alertConfigQueryKey,
  api,
  metricQueryKey,
  universeQueryKey
} from '../../../../redesign/helpers/api';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import {
  DEFAULT_METRIC_TIME_RANGE_OPTION,
  MetricName,
  METRIC_TIME_RANGE_OPTIONS,
  PollingIntervalMs,
  TimeRangeType,
  XClusterConfigType
} from '../../constants';
import {
  adaptMetricDataForRecharts,
  formatUuidFromXCluster,
  getMetricTimeRange,
  getStrictestReplicationLagAlertThreshold
} from '../../ReplicationUtils';
import { CustomDatePicker } from '../../../metrics/CustomDatePicker/CustomDatePicker';
import { getAlertConfigurations } from '../../../../actions/universe';
import { YBTooltip } from '../../../../redesign/components';
import { YBMetricGraph } from '../../../../redesign/components/YBMetrics/YBMetricGraph';
import { getUniqueTraceId } from '../../../../redesign/components/YBMetrics/utils';
import { MetricsFilter } from '../../../../redesign/components/YBMetrics/MetricsFilter';

import { MetricTimeRangeOption } from '../../XClusterTypes';
import { XClusterConfig } from '../../dtos';
import {
  MetricSettings,
  MetricsQueryParams,
  MetricsQueryResponse,
  MetricTrace
} from '../../../../redesign/helpers/dtos';
import { NodeAggregation, SplitMode, SplitType } from '../../../metrics/dtos';
import {
  AlertTemplate,
  IAlertConfiguration as AlertConfiguration
} from '../../../../redesign/features/alerts/TemplateComposer/ICustomVariables';

interface ConfigReplicationLagGraphProps {
  xClusterConfig: XClusterConfig;
  isDrInterface: boolean;
}

const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.metricsPanel';

export const XClusterMetrics = ({
  xClusterConfig,
  isDrInterface
}: ConfigReplicationLagGraphProps) => {
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
    template: AlertTemplate.REPLICATION_LAG,
    targetUuid: xClusterConfig.sourceUniverseUUID
  };
  const alertConfigQuery = useQuery<AlertConfiguration[]>(
    alertConfigQueryKey.list(alertConfigFilter),
    () => getAlertConfigurations(alertConfigFilter)
  );
  const maxAcceptableLag = getStrictestReplicationLagAlertThreshold(alertConfigQuery.data);
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
  const replicationLagMetricRequestParams: MetricsQueryParams = {
    metricsWithSettings: [replicationLagMetricSettings],
    nodePrefix: sourceUniverseQuery.data?.universeDetails.nodePrefix,
    xClusterConfigUuid: xClusterConfig.uuid,
    start: metricTimeRange.startMoment.format('X'),
    end: metricTimeRange.endMoment.format('X')
  };
  const configReplicationLagMetricQuery = useQuery(
    isFixedTimeRange
      ? metricQueryKey.detail(replicationLagMetricRequestParams)
      : metricQueryKey.live(
          replicationLagMetricRequestParams,
          selectedTimeRangeOption.value,
          selectedTimeRangeOption.type
        ),
    () => api.fetchMetrics(replicationLagMetricRequestParams),
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
      : metricQueryKey.live(
          consumerSafeTimeLagMetricRequestParams,
          selectedTimeRangeOption.value,
          selectedTimeRangeOption.type
        ),
    () => api.fetchMetrics(consumerSafeTimeLagMetricRequestParams),
    {
      enabled: !!targetUniverseQuery.data && xClusterConfig.type === XClusterConfigType.TXN,
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
      : metricQueryKey.live(
          consumerSafeTimeSkewMetricRequestParams,
          selectedTimeRangeOption.value,
          selectedTimeRangeOption.type
        ),
    () => api.fetchMetrics(consumerSafeTimeSkewMetricRequestParams),
    {
      enabled: !!targetUniverseQuery.data && xClusterConfig.type === XClusterConfigType.TXN,
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
        customErrorMessage={t(
          isDrInterface ? 'failedToFetchDrPrimaryUniverse' : 'failedToFetchSourceUniverse',
          {
            keyPrefix: 'queryError',
            universeUuid: xClusterConfig.sourceUniverseUUID
          }
        )}
      />
    );
  }
  if (targetUniverseQuery.isError || targetUniverseNamespaceQuery.isError) {
    const i18nKey = isDrInterface
      ? targetUniverseQuery.isError
        ? 'failedToFetchDrReplicaUniverse'
        : 'failedToFetchDrReplicaNamespaces'
      : targetUniverseQuery.isError
      ? 'failedToFetchTargetUniverse'
      : 'failedToFetchTargetUniverseNamespaces';
    return (
      <YBErrorIndicator
        customErrorMessage={t(i18nKey, {
          keyPrefix: 'queryError',
          universeUuid: xClusterConfig.targetUniverseUUID
        })}
      />
    );
  }
  // IMPROVEMENT: Isolate the error message to the graph for which metrics failed to load.
  //              Draw a placeholder graph showing no data and an error message.
  //              Tracking with PLAT-11663
  if (
    configReplicationLagMetricQuery.isError ||
    (xClusterConfig.type === XClusterConfigType.TXN &&
      (consumerSafeTimeLagMetricsQuery.isError || consumerSafeTimeSkewMetricsQuery.isError))
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

  const refetchMetrics = () => {
    configReplicationLagMetricQuery.refetch();
    if (!!targetUniverseQuery.data && xClusterConfig.type === XClusterConfigType.TXN) {
      consumerSafeTimeLagMetricsQuery.refetch();
      consumerSafeTimeSkewMetricsQuery.refetch();
    }
  };

  const namespaceUuidToNamespaceName = targetUniverseNamespaceQuery.data
    ? Object.fromEntries(
        targetUniverseNamespaceQuery.data.map((namespace) => [
          namespace.namespaceUUID,
          namespace.name
        ])
      )
    : {};
  const configReplicationLagMetrics = {
    ...(configReplicationLagMetricQuery.data ?? {
      metricData: [],
      layout: undefined,
      metricTraces: []
    }),
    metricTraces: getFilteredMetricTraces(
      MetricName.ASYNC_REPLICATION_SENT_LAG,
      replicationLagMetricSettings,
      configReplicationLagMetricQuery.data?.metricTraces ?? [],
      namespaceUuidToNamespaceName
    )
  };
  const consumerSafeTimeLagMetrics = {
    ...(consumerSafeTimeLagMetricsQuery.data ?? {
      metricData: [],
      layout: undefined,
      metricTraces: []
    }),
    metricTraces: getFilteredMetricTraces(
      MetricName.CONSUMER_SAFE_TIME_LAG,
      consumerSafeTimeLagMetricSettings,
      consumerSafeTimeLagMetricsQuery.data?.metricTraces ?? [],
      namespaceUuidToNamespaceName
    )
  };
  const consumerSafeTimeSkewMetrics = {
    ...(consumerSafeTimeSkewMetricsQuery.data ?? {
      metricData: [],
      layout: undefined,
      metricTraces: []
    }),
    metricTraces: getFilteredMetricTraces(
      MetricName.CONSUMER_SAFE_TIME_SKEW,
      consumerSafeTimeSkewMetricSettings,
      consumerSafeTimeSkewMetricsQuery.data?.metricTraces ?? [],
      namespaceUuidToNamespaceName
    )
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
              handleTimeframeChange={refetchMetrics}
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
        <YBMetricGraph
          metric={configReplicationLagMetrics}
          title={t('graphTitle.asyncReplicationSentLag')}
          metricSettings={replicationLagMetricSettings}
          unit={t('unitAbbreviation.milliseconds', { keyPrefix: 'common' })}
          namespaceUuidToNamespaceName={namespaceUuidToNamespaceName}
          referenceLines={
            maxAcceptableLag && showAlertThresholdReferenceLine
              ? [
                  {
                    y: maxAcceptableLag,
                    name: i18next.t(
                      'clusterDetail.xCluster.metrics.lowestReplicationLagAlertThresholdLabel'
                    ),
                    strokeColor: '#EF5824',
                    ifOverflow: 'extendDomain'
                  }
                ]
              : []
          }
          graphHeaderAccessor={
            <>
              <YBTooltip
                title={
                  maxAcceptableLag === undefined
                    ? `${i18next.t(
                        'clusterDetail.xCluster.metrics.showAlertThresholdReferenceLine.disabledTooltip'
                      )}`
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
                        {i18next.t(
                          'clusterDetail.xCluster.metrics.showAlertThresholdReferenceLine.label'
                        )}
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
            </>
          }
        />
        {xClusterConfig.type === XClusterConfigType.TXN && (
          <>
            <YBMetricGraph
              metric={consumerSafeTimeLagMetrics}
              title={t('graphTitle.consumerSafeTimeLag')}
              metricSettings={consumerSafeTimeLagMetricSettings}
              unit={t('unitAbbreviation.milliseconds', { keyPrefix: 'common' })}
              namespaceUuidToNamespaceName={namespaceUuidToNamespaceName}
              graphHeaderAccessor={
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
              }
            />
            <YBMetricGraph
              metric={consumerSafeTimeSkewMetrics}
              title={t('graphTitle.consumerSafeTimeSkew')}
              metricSettings={consumerSafeTimeSkewMetricSettings}
              unit={t('unitAbbreviation.milliseconds', { keyPrefix: 'common' })}
              namespaceUuidToNamespaceName={namespaceUuidToNamespaceName}
              graphHeaderAccessor={
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
              }
            />
          </>
        )}
      </Box>
    </div>
  );
};

const getFilteredMetricTraces = (
  metric: MetricName,
  metricSettings: MetricSettings,
  metricTraces: MetricTrace[],
  namespaceUuidToNamespace: { [namespaceUuid: string]: string | undefined }
): MetricTrace[] => {
  /*
   * Consumer safe time lag and consumer safe time skew may contain metric traces associated
   * with namespaces which are already dropped from the target universe.
   * If the time range is large and we have many dropped namespaces, then the legend for the metric
   * graph will become busy.
   */
  if (
    metricSettings.splitType === SplitType.NAMESPACE &&
    ([
      MetricName.CONSUMER_SAFE_TIME_LAG,
      MetricName.CONSUMER_SAFE_TIME_SKEW
    ] as MetricName[]).includes(metric)
  ) {
    return metricTraces.filter(
      (trace) =>
        !trace.namespaceId || namespaceUuidToNamespace[formatUuidFromXCluster(trace.namespaceId)]
    );
  }
  return metricTraces;
};

import { useCallback, useState } from 'react';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import i18next from 'i18next';
import moment from 'moment';
import { Box, Typography, useTheme } from '@material-ui/core';
import { Dropdown, MenuItem } from 'react-bootstrap';
import { ToggleButton } from '@material-ui/lab';

import { getTableName, getTableUuid } from '../../../utils/tableUtils';
import { YBMetricGraph } from '../../../redesign/components/YBMetrics/YBMetricGraph';
import {
  DEFAULT_METRIC_TIME_RANGE_OPTION,
  MetricName,
  METRIC_TIME_RANGE_OPTIONS,
  PollingIntervalMs,
  TimeRangeType
} from '../constants';
import { NodeAggregation, SplitMode, SplitType } from '../../metrics/dtos';
import { CustomDatePicker } from '../../metrics/CustomDatePicker/CustomDatePicker';
import {
  adaptMetricDataForRecharts,
  getMetricTimeRange,
  getStrictestReplicationLagAlertThreshold
} from '../ReplicationUtils';
import { alertConfigQueryKey, api, metricQueryKey } from '../../../redesign/helpers/api';
import {
  MetricSettings,
  MetricsQueryParams,
  MetricsQueryResponse,
  MetricTrace
} from '../../../redesign/helpers/dtos';
import { getUniqueTraceId } from '../../../redesign/components/YBMetrics/utils';
import {
  AlertTemplate,
  IAlertConfiguration as AlertConfiguration
} from '../../../redesign/features/alerts/TemplateComposer/ICustomVariables';
import { getAlertConfigurations } from '../../../actions/universe';
import { YBModal, YBModalProps, YBTooltip } from '../../../redesign/components';

import { MetricTimeRangeOption, XClusterTable } from '../XClusterTypes';

interface TableReplicationLagGraphModalProps {
  xClusterTable: XClusterTable;
  queryEnabled: boolean;
  sourceUniverseUuid: string;
  nodePrefix: string;
  modalProps: YBModalProps;
}

const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.tableLevelMetricModal';
export const TableReplicationLagGraphModal = ({
  xClusterTable,
  sourceUniverseUuid,
  queryEnabled,
  nodePrefix,
  modalProps
}: TableReplicationLagGraphModalProps) => {
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
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const theme = useTheme();

  const alertConfigFilter = {
    template: AlertTemplate.REPLICATION_LAG,
    targetUuid: sourceUniverseUuid
  };
  const alertConfigQuery = useQuery<AlertConfiguration[]>(
    alertConfigQueryKey.list(alertConfigFilter),
    () => getAlertConfigurations(alertConfigFilter)
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
    nodeAggregation: NodeAggregation.MAX,
    splitType: SplitType.TABLE,
    splitMode: SplitMode.NONE
  };
  const replciationLagMetricRequestParams: MetricsQueryParams = {
    metricsWithSettings: [replicationLagMetricSettings],
    nodePrefix: nodePrefix,
    streamId: xClusterTable.streamId,
    tableId: getTableUuid(xClusterTable),
    start: metricTimeRange.startMoment.format('X'),
    end: metricTimeRange.endMoment.format('X')
  };
  const configReplicationLagMetricQuery = useQuery(
    isFixedTimeRange
      ? metricQueryKey.detail(replciationLagMetricRequestParams)
      : metricQueryKey.live(
          replciationLagMetricRequestParams,
          selectedTimeRangeOption.value,
          selectedTimeRangeOption.type
        ),
    () => api.fetchMetrics(replciationLagMetricRequestParams),
    {
      enabled: queryEnabled,
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
  const handleTimeRangeChange = (eventKey: any) => {
    const selectedOption = METRIC_TIME_RANGE_OPTIONS[eventKey];
    if (selectedOption.type !== 'divider') {
      setSelectedTimeRangeOption(selectedOption);
    }
  };
  const handleToggleShowAlertThresholdReferenceLine = () =>
    setShowAlertThresholdReferenceLIne(!showAlertThresholdReferenceLine);

  const getUniqueTraceName = (_: MetricSettings, trace: MetricTrace) =>
    `${i18next.t(`prometheusMetricTrace.${trace.name}`)} (${xClusterTable.keySpace}/${getTableName(
      xClusterTable
    )})`;

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
  const configReplicationLagMetrics = {
    ...(configReplicationLagMetricQuery.data ?? {
      metricData: [],
      layout: undefined,
      metricTraces: []
    })
  };
  const maxAcceptableLag = getStrictestReplicationLagAlertThreshold(alertConfigQuery.data);
  return (
    <YBModal title={t('title')} size="xl" {...modalProps}>
      <p>
        {`${t('label.table')}: `}
        <b>{getTableName(xClusterTable)}</b>
      </p>
      {xClusterTable.pgSchemaName && (
        <p>
          {`${t('label.schema')}: `}
          <b>{xClusterTable.pgSchemaName}</b>
        </p>
      )}
      <p>
        {`${t('label.database')}: `}
        <b>{xClusterTable.keySpace}</b>
      </p>
      <Box display="flex" marginBottom={2}>
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
      <YBMetricGraph
        metric={configReplicationLagMetrics}
        title={t('graphTitle.asyncReplicationSentLag')}
        metricSettings={replicationLagMetricSettings}
        getUniqueTraceNameOverride={getUniqueTraceName}
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
        }
      />
    </YBModal>
  );
};

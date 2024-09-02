import { Box } from '@material-ui/core';
import i18next from 'i18next';
import moment from 'moment';
import { useCallback, useState } from 'react';
import { Dropdown, MenuItem } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';

import { getUniqueTraceId } from '../../redesign/components/YBMetrics/utils';
import { YBMetricGraph } from '../../redesign/components/YBMetrics/YBMetricGraph';
import { api, metricQueryKey } from '../../redesign/helpers/api';
import {
  MetricSettings,
  MetricsQueryParams,
  MetricsQueryResponse,
  MetricTrace
} from '../../redesign/helpers/dtos';
import { YBErrorIndicator } from '../common/indicators';
import { CustomDatePicker } from '../metrics/CustomDatePicker/CustomDatePicker';
import { MetricTimeRangeOption } from '../xcluster';
import {
  DEFAULT_METRIC_TIME_RANGE_OPTION,
  MetricName,
  METRIC_TIME_RANGE_OPTIONS,
  TimeRangeType
} from '../xcluster/constants';
import { adaptMetricDataForRecharts, getMetricTimeRange } from '../xcluster/ReplicationUtils';

const TRANSLATION_KEY_PREFIX = 'ha.metricsPanel';

export const HaMetrics = () => {
  const [selectedTimeRangeOption, setSelectedTimeRangeOption] = useState<MetricTimeRangeOption>(
    DEFAULT_METRIC_TIME_RANGE_OPTION
  );
  const [customStartMoment, setCustomStartMoment] = useState(
    getMetricTimeRange(DEFAULT_METRIC_TIME_RANGE_OPTION).startMoment
  );
  const [customEndMoment, setCustomEndMoment] = useState(
    getMetricTimeRange(DEFAULT_METRIC_TIME_RANGE_OPTION).endMoment
  );

  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const isCustomTimeRange = selectedTimeRangeOption.type === TimeRangeType.CUSTOM;
  const metricTimeRange = isCustomTimeRange
    ? { startMoment: customStartMoment, endMoment: customEndMoment }
    : getMetricTimeRange(selectedTimeRangeOption);
  // At the moment, we don't support a custom time range which uses the 'current time' as the end time.
  // Thus, all custom time ranges are fixed.
  const isFixedTimeRange = isCustomTimeRange;
  const haBackupLagMetricSettings = {
    metric: MetricName.HA_BACKUP_LAG
  };
  const haBackupLagMetricRequestParams: MetricsQueryParams = {
    metricsWithSettings: [haBackupLagMetricSettings],
    start: metricTimeRange.startMoment.format('X'),
    end: metricTimeRange.endMoment.format('X')
  };
  const haLastBackupSecondsMetricQuery = useQuery(
    isFixedTimeRange
      ? metricQueryKey.detail(haBackupLagMetricRequestParams)
      : metricQueryKey.live(
          haBackupLagMetricRequestParams,
          selectedTimeRangeOption.value,
          selectedTimeRangeOption.type
        ),
    () => api.fetchMetrics(haBackupLagMetricRequestParams),
    {
      // It is unnecessary to refetch metric traces when the interval is fixed as subsequent
      // queries will return the same data.
      staleTime: isFixedTimeRange ? Infinity : 0,
      refetchInterval: isFixedTimeRange ? false : 15_000,
      select: useCallback((metricQueryResponse: MetricsQueryResponse) => {
        const { data: metricTraces = [], ...metricQueryMetadata } =
          metricQueryResponse.yba_ha_backup_lag ?? {};
        return {
          ...metricQueryMetadata,
          metricTraces,
          metricData: adaptMetricDataForRecharts(metricTraces, (trace) => getUniqueTraceId(trace))
        };
      }, [])
    }
  );
  const haLastBackupSizeMetricSettings = {
    metric: MetricName.HA_LAST_BACKUP_SIZE
  };
  const haLastBackupSizeMetricRequestParams: MetricsQueryParams = {
    metricsWithSettings: [haLastBackupSizeMetricSettings],
    start: metricTimeRange.startMoment.format('X'),
    end: metricTimeRange.endMoment.format('X')
  };
  const haLastBackupSizeMetricQuery = useQuery(
    isFixedTimeRange
      ? metricQueryKey.detail(haLastBackupSizeMetricRequestParams)
      : metricQueryKey.live(
          haLastBackupSizeMetricRequestParams,
          selectedTimeRangeOption.value,
          selectedTimeRangeOption.type
        ),
    () => api.fetchMetrics(haLastBackupSizeMetricRequestParams),
    {
      // It is unnecessary to refetch metric traces when the interval is fixed as subsequent
      // queries will return the same data.
      staleTime: isFixedTimeRange ? Infinity : 0,
      refetchInterval: isFixedTimeRange ? false : 15_000,
      select: useCallback((metricQueryResponse: MetricsQueryResponse) => {
        const { data: metricTraces = [], ...metricQueryMetadata } =
          metricQueryResponse.yba_ha_last_backup_size_mb ?? {};
        return {
          ...metricQueryMetadata,
          metricTraces,
          metricData: adaptMetricDataForRecharts(metricTraces, (trace) => getUniqueTraceId(trace))
        };
      }, [])
    }
  );

  if (haLastBackupSecondsMetricQuery.isError || haLastBackupSizeMetricQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('failedToFetchMetrics', {
          keyPrefix: 'queryError'
        })}
      />
    );
  }

  const refetchMetrics = () => {
    haLastBackupSecondsMetricQuery.refetch();
    haLastBackupSizeMetricQuery.refetch();
  };
  const getUniqueTraceName = (_: MetricSettings, trace: MetricTrace) =>
    `${i18next.t(`prometheusMetricTrace.${trace.metricName}`)} ${
      trace.ybaInstanceAddress ? `(${trace.ybaInstanceAddress})` : ''
    }`;
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
  const haLastBackupSecondsMetrics = {
    ...(haLastBackupSecondsMetricQuery.data ?? {
      metricData: [],
      layout: undefined,
      metricTraces: []
    })
  };
  const haLastBackupSizeMetrics = {
    ...(haLastBackupSizeMetricQuery.data ?? {
      metricData: [],
      layout: undefined,
      metricTraces: []
    })
  };
  return (
    <div>
      <Box display="flex" marginBottom={2}>
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
      <YBMetricGraph
        metric={haLastBackupSecondsMetrics}
        title={t('graphTitle.haBackupLag')}
        getUniqueTraceNameOverride={getUniqueTraceName}
        metricSettings={haBackupLagMetricSettings}
        unit={t('unitAbbreviation.seconds', { keyPrefix: 'common' })}
      />
      <YBMetricGraph
        metric={haLastBackupSizeMetrics}
        title={t('graphTitle.haLastBackupSize')}
        getUniqueTraceNameOverride={getUniqueTraceName}
        metricSettings={haBackupLagMetricSettings}
        unit={t('unitAbbreviation.megabyte', { keyPrefix: 'common' })}
      />
    </div>
  );
};

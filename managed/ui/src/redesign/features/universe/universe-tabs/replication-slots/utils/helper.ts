import axios from 'axios';
import moment from 'moment';
import { ROOT_URL } from '../../../../../../config';
import { ReplicationSlot, metricsResponse } from './types';

export const TIME_RANGE_FILTER_TYPES = [
  { label: 'Last 1 hr', type: 'hours', value: '1' },
  { label: 'Last 6 hrs', type: 'hours', value: '6' },
  { label: 'Last 12 hrs', type: 'hours', value: '12' },
  { label: 'Last 24 hrs', type: 'hours', value: '24' },
  { label: 'Last 7 days', type: 'days', value: '7' },
  { type: 'divider', label: 'divider', value: 'divider' },
  { label: 'Custom', type: 'custom', value: 'custom' }
];

export const INTERVAL_TYPES = [
  { label: 'Off', selectedLabel: 'Off', value: 'off' },
  { label: 'Every 1 minute', selectedLabel: '1 minute', value: 60000 },
  { label: 'Every 2 minutes', selectedLabel: '2 minute', value: 120000 }
];

export const CDC_LAG_KEY = 'cdcsdk_sent_lag_micros';
export const CDC_CHANGE_EVENT_KEY = 'cdcsdk_change_event_count';
export const CDC_TRAFFIC_SENT_KEY = 'cdcsdk_traffic_sent';
export const CDC_EXPIRY_TIME_KEY = 'cdcsdk_expiry_time_mins';

export const CDC_METRIC_ARRAY = [
  CDC_LAG_KEY,
  CDC_CHANGE_EVENT_KEY,
  CDC_TRAFFIC_SENT_KEY,
  CDC_EXPIRY_TIME_KEY
];

export const CDC_METRICS_COLOR = {
  [CDC_LAG_KEY]: '#0098F0',
  [CDC_CHANGE_EVENT_KEY]: '#0098F0',
  [CDC_TRAFFIC_SENT_KEY]: '#1C1CE7',
  [CDC_EXPIRY_TIME_KEY]: '#E48B2C'
};

export const fetchCDCLagMetrics = async (streamID: string, nodePrefix: string) => {
  const customerId = localStorage.getItem('customerId');
  const defaultFilter = {
    metrics: CDC_METRIC_ARRAY,
    streamId: streamID,
    nodePrefix,
    start: moment().utc().subtract('1', 'hour').format('X'),
    end: moment().utc().format('X')
  };

  return await axios
    .post<metricsResponse>(`${ROOT_URL}/customers/${customerId}/metrics`, defaultFilter)
    .then((resp) => ({ ...resp.data, streamID }));
};

export const fetchCDCAllMetrics = async (graphFilter: Record<string, any>) => {
  const customerId = localStorage.getItem('customerId');
  const defaultFilter = {
    metrics: CDC_METRIC_ARRAY,
    ...graphFilter
  };
  return await axios
    .post<metricsResponse>(`${ROOT_URL}/customers/${customerId}/metrics`, defaultFilter)
    .then((resp) => ({ ...resp.data, streamID: graphFilter.streamId }));
};

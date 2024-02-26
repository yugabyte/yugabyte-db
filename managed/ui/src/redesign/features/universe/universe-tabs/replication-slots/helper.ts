import axios from 'axios';
import moment from 'moment';
import { ROOT_URL } from '../../../../../config';
import { ReplicationSlot, metricsResponse } from './types';

export const fetchCurrentLag = async (data: Partial<ReplicationSlot>[], nodePrefix: string) => {
  const customerId = localStorage.getItem('customerId');
  const currentLagResponse = await Promise.all(
    data.map((e) => {
      const defaultFilter = {
        metrics: [
          'cdcsdk_sent_lag_micros',
          'cdcsdk_expiry_time_mins',
          'cdcsdk_change_event_count',
          'cdcsdk_traffic_sent'
        ],
        streamId: e.streamID,
        nodePrefix,
        start: moment().utc().subtract('2', 'hour').format('X'),
        end: moment().utc().format('X')
      };
      return axios
        .post<ReplicationSlot[]>(`${ROOT_URL}/customers/${customerId}/metrics`, defaultFilter)
        .then((resp) => ({ ...e, metric: resp.data }));
    })
  );
  return currentLagResponse;
};

export const fetchCDCMetrics = async (streamID: string, nodePrefix: string) => {
  const customerId = localStorage.getItem('customerId');
  const defaultFilter = {
    metrics: ['cdcsdk_sent_lag_micros'],
    streamId: streamID,
    nodePrefix,
    start: moment().utc().subtract('1', 'hour').format('X'),
    end: moment().utc().format('X')
  };

  return await axios
    .post<metricsResponse>(`${ROOT_URL}/customers/${customerId}/metrics`, defaultFilter)
    .then((resp) => ({ ...resp.data, streamID }));
};

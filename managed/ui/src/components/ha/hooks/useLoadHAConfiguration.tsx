import _ from 'lodash';
import { useQuery, useQueryClient } from 'react-query';
import { AxiosError } from 'axios';
import { api, QUERY_KEY } from '../../../redesign/helpers/api';
import { HaConfig } from '../dtos';

interface LoadHAOptions {
  loadSchedule: boolean;
  autoRefresh: boolean;
}

export const REFETCH_INTERVAL_MS = 5000;

export const useLoadHAConfiguration = ({ loadSchedule, autoRefresh }: LoadHAOptions) => {
  const queryClient = useQueryClient();
  const cachedConfig = queryClient.getQueryData<HaConfig>(QUERY_KEY.getHAConfig);

  // enable refetching when HA config already available, otherwise no point in auto-refreshing
  const refetchInterval = autoRefresh && !_.isEmpty(cachedConfig) && REFETCH_INTERVAL_MS;

  // load HA config
  const {
    isLoading: isLoadingConfig,
    error: errorConfig,
    data: config
  } = useQuery(QUERY_KEY.getHAConfig, api.getHAConfig, { refetchInterval });

  // once HA config is there - load its replication schedule (if requested)
  const { isLoading: isLoadingSchedule, error: errorSchedule, data: schedule } = useQuery(
    [QUERY_KEY.getHAReplicationSchedule, config?.uuid],
    () => api.getHAReplicationSchedule(config?.uuid),
    { enabled: loadSchedule && !!config?.uuid }
  );

  const isLoading = isLoadingConfig || isLoadingSchedule;
  const isNoHAConfigExists = (errorConfig as AxiosError)?.response?.status === 404;

  let error = null;
  if ((errorConfig && !isNoHAConfigExists) || errorSchedule) {
    error = errorConfig || errorSchedule;
  }

  return { config, schedule, error, isNoHAConfigExists, isLoading };
};

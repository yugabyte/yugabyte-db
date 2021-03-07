import { useQuery } from 'react-query';
import { AxiosError } from 'axios';
import { api, QUERY_KEY } from '../../../redesign/helpers/api';

export const useLoadHAConfiguration = (loadSchedule: boolean) => {
  const { isFetching: isLoadingConfig, error: errorConfig, data: config } = useQuery(
    QUERY_KEY.getHAConfig,
    api.getHAConfig
  );
  // once HA config is there - load its replication schedule (if asked)
  const { isFetching: isLoadingSchedule, error: errorSchedule, data: schedule } = useQuery(
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

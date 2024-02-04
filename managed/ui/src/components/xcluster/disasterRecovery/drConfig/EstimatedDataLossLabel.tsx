import { Typography } from '@material-ui/core';
import { useQuery } from 'react-query';

import { PollingIntervalMs } from '../../constants';
import { YBLoading } from '../../../common/indicators';
import { api, drConfigQueryKey } from '../../../../redesign/helpers/api';
import { formatDuration } from '../../../../utils/Formatters';

interface EstimatedDataLossLabelProps {
  drConfigUuid: string;
}

export const EstimatedDataLossLabel = ({ drConfigUuid }: EstimatedDataLossLabelProps) => {
  const currentSafetimesQuery = useQuery(
    drConfigQueryKey.safetimes(drConfigUuid),
    () => api.fetchCurrentSafetimes(drConfigUuid),
    { refetchInterval: PollingIntervalMs.XCLUSTER_METRICS }
  );

  if (currentSafetimesQuery.isLoading) {
    return <YBLoading />;
  }

  const safeTimes = currentSafetimesQuery.data?.safetimes;
  const maxEstimatedDataLossMs =
    safeTimes && safeTimes.length > 0
      ? Math.max(...safeTimes.map((safetime) => safetime.estimatedDataLossMs ?? -1))
      : -1;
  // `estimatedDataLossMs` is reported as -1 if we can't find it from the Prometheus metrics.
  return (
    <Typography variant="body2">
      {maxEstimatedDataLossMs === -1 ? 'Not Reported' : formatDuration(maxEstimatedDataLossMs)}
    </Typography>
  );
};

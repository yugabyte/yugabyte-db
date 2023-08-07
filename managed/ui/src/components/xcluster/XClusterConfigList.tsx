import _ from 'lodash';
import clsx from 'clsx';
import { useQueries, useQuery, useQueryClient, UseQueryResult } from 'react-query';
import { useInterval } from 'react-use';
import { Typography } from '@material-ui/core';

import { fetchXClusterConfig } from '../../actions/xClusterReplication';
import { YBErrorIndicator, YBLoading, YBLoadingCircleIcon } from '../common/indicators';
import {
  TRANSITORY_XCLUSTER_CONFIG_STATUSES,
  XCLUSTER_METRIC_REFETCH_INTERVAL_MS
} from './constants';
import { XClusterConfigCard } from './XClusterConfigCard';
import { api, xClusterQueryKey } from '../../redesign/helpers/api';
import { XClusterConfig } from './XClusterTypes';

import styles from './XClusterConfigList.module.scss';

interface Props {
  currentUniverseUUID: string;
}

export function XClusterConfigList({ currentUniverseUUID }: Props) {
  const queryClient = useQueryClient();

  const universeQuery = useQuery(['universe', currentUniverseUUID], () =>
    api.fetchUniverse(currentUniverseUUID)
  );

  const sourceXClusterConfigUUIDs =
    universeQuery.data?.universeDetails?.xclusterInfo?.sourceXClusterConfigs ?? [];
  const targetXClusterConfigUUIDs =
    universeQuery.data?.universeDetails?.xclusterInfo?.targetXClusterConfigs ?? [];

  // List the XCluster Configurations for which the current universe is a source or a target.
  const universeXClusterConfigUUIDs: string[] = [
    ...sourceXClusterConfigUUIDs,
    ...targetXClusterConfigUUIDs
  ];

  // The unsafe cast is needed due to issue with useQueries typing
  // Upgrading react-query to v3.28 may solve this issue: https://github.com/TanStack/query/issues/1675
  const xClusterConfigQueries = useQueries(
    universeXClusterConfigUUIDs.map((uuid: string) => ({
      queryKey: xClusterQueryKey.detail(uuid),
      queryFn: () => fetchXClusterConfig(uuid),
      enabled: universeQuery.data?.universeDetails !== undefined
    }))
  ) as UseQueryResult<XClusterConfig>[];

  useInterval(() => {
    xClusterConfigQueries.forEach((xClusterConfig) => {
      if (
        xClusterConfig?.data?.status &&
        _.includes(TRANSITORY_XCLUSTER_CONFIG_STATUSES, xClusterConfig.data.status)
      ) {
        queryClient.invalidateQueries(xClusterQueryKey.detail(xClusterConfig.data.uuid));
      }
    });
  }, XCLUSTER_METRIC_REFETCH_INTERVAL_MS);

  if (universeQuery.isLoading || universeQuery.isIdle) {
    return <YBLoading />;
  }
  if (universeQuery.isError) {
    return <YBErrorIndicator />;
  }

  return (
    <>
      <ul className={styles.listContainer}>
        {xClusterConfigQueries.length === 0 ? (
          <div className={clsx(styles.configCard, styles.emptyConfigListPlaceholder)}>
            No replications to show.
          </div>
        ) : (
          xClusterConfigQueries.map((xClusterConfigQuery, index) => {
            const xClusterConfigUUID = universeXClusterConfigUUIDs[index];
            if (xClusterConfigQuery.isLoading) {
              return (
                <li className={clsx(styles.listItem)} key={xClusterConfigUUID}>
                  <div className={(styles.configCard, styles.loading)}>
                    <YBLoadingCircleIcon />
                  </div>
                </li>
              );
            }
            if (xClusterConfigQuery.isError) {
              return (
                <li className={styles.listItem} key={xClusterConfigUUID}>
                  <div className={clsx(styles.configCard, styles.error)}>
                    <i className="fa fa-exclamation-triangle" />
                    <Typography variant="h5">
                      {`Error fetching xCluster configuration: ${xClusterConfigUUID}`}
                    </Typography>
                  </div>
                </li>
              );
            }
            if (!xClusterConfigQuery.isError && xClusterConfigQuery.data) {
              return (
                <li className={styles.listItem} key={xClusterConfigQuery.data.uuid}>
                  <XClusterConfigCard
                    xClusterConfig={xClusterConfigQuery.data}
                    currentUniverseUUID={currentUniverseUUID}
                  />
                </li>
              );
            }
            return null;
          })
        )}
      </ul>
    </>
  );
}

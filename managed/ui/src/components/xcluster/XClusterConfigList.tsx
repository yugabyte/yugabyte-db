import React from 'react';
import _ from 'lodash';
import clsx from 'clsx';
import { useQueries, useQuery, useQueryClient, UseQueryResult } from 'react-query';
import { useSelector } from 'react-redux';
import { useInterval } from 'react-use';

import { getXclusterConfig } from '../../actions/xClusterReplication';
import { YBErrorIndicator, YBLoading, YBLoadingCircleIcon } from '../common/indicators';
import { TRANSITORY_STATES, XCLUSTER_CONFIG_REFETCH_INTERVAL_MS } from './constants';
import { XClusterConfigCard } from './XClusterConfigCard';
import { api } from '../../redesign/helpers/api';

import { XClusterConfig } from './XClusterTypes';

import styles from './XClusterConfigList.module.scss';

interface Props {
  currentUniverseUUID: string;
}

export function XClusterConfigList({ currentUniverseUUID }: Props) {
  const currentUserTimezone = useSelector(
    (state: any) => state?.customer?.currentUser?.data?.timezone
  );
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
      queryKey: ['Xcluster', uuid],
      queryFn: () => getXclusterConfig(uuid),
      enabled: universeQuery.data?.universeDetails !== undefined
    }))
  ) as UseQueryResult<XClusterConfig>[];

  useInterval(() => {
    xClusterConfigQueries.forEach((xClusterConfig: any) => {
      if (
        xClusterConfig?.data?.status &&
        _.includes(TRANSITORY_STATES, xClusterConfig.data.status)
      ) {
        queryClient.invalidateQueries('Xcluster');
      }
    });
  }, XCLUSTER_CONFIG_REFETCH_INTERVAL_MS);

  if (universeQuery.isLoading) {
    return <YBLoading />;
  }
  if (universeQuery.isError || universeQuery.data === undefined) {
    return <YBErrorIndicator />;
  }

  return (
    <ul className={styles.listContainer}>
      {xClusterConfigQueries.length === 0 ? (
        <div className={clsx(styles.configCard, styles.emptyConfigListPlaceholder)}>
          No replications to show.
        </div>
      ) : (
        xClusterConfigQueries.map((xClusterConfigQuery, index) => {
          if (xClusterConfigQuery.isLoading) {
            return (
              <li
                className={clsx(styles.listItem, styles.loading)}
                key={universeXClusterConfigUUIDs[index]}
              >
                <div className={styles.configCard}>
                  <YBLoadingCircleIcon />
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
                  currentUserTimezone={currentUserTimezone}
                />
              </li>
            );
          }
          return null;
        })
      )}
    </ul>
  );
}

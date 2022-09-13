import React from 'react';
import _ from 'lodash';
import clsx from 'clsx';
import { useQueries, useQuery, useQueryClient, UseQueryResult } from 'react-query';
import { useSelector } from 'react-redux';
import { useInterval } from 'react-use';

import { getUniverseInfo, getXclusterConfig } from '../../actions/xClusterReplication';
import { YBLoading, YBLoadingCircleIcon } from '../common/indicators';
import { TRANSITORY_STATES, XCLUSTER_CONFIG_REFETCH_INTERVAL_MS } from './constants';
import { XClusterConfigCard } from './XClusterConfigCard';

import { Replication } from './XClusterTypes';

import styles from './XClusterConfigList.module.scss';

interface Props {
  currentUniverseUUID: string;
}

export function XClusterConfigList({ currentUniverseUUID }: Props) {
  const currentUserTimezone = useSelector(
    (state: any) => state?.customer?.currentUser?.data?.timezone
  );
  const queryClient = useQueryClient();

  const { data: universeInfo, isLoading: currentUniverseLoading } = useQuery(
    ['universe', currentUniverseUUID],
    () => getUniverseInfo(currentUniverseUUID)
  );

  const sourceXClusterConfigUUIDs =
    universeInfo?.data?.universeDetails?.sourceXClusterConfigs ?? [];
  const targetXClusterConfigUUIDs =
    universeInfo?.data?.universeDetails?.targetXClusterConfigs ?? [];

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
      enabled: universeInfo?.data !== undefined
    }))
  ) as UseQueryResult<Replication>[];

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

  if (currentUniverseLoading) {
    return <YBLoading />;
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

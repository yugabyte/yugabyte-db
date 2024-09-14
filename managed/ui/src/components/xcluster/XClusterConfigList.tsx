import _ from 'lodash';
import clsx from 'clsx';
import { useQueries, useQuery, useQueryClient, UseQueryResult } from 'react-query';
import { useInterval } from 'react-use';
import { Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';

import { fetchXClusterConfig } from '../../actions/xClusterReplication';
import { YBErrorIndicator, YBLoading, YBLoadingCircleIcon } from '../common/indicators';
import {
  TRANSITORY_XCLUSTER_CONFIG_STATUSES,
  XCLUSTER_METRIC_REFETCH_INTERVAL_MS
} from './constants';
import { XClusterConfigCard } from './XClusterConfigCard';
import {
  api,
  runtimeConfigQueryKey,
  universeQueryKey,
  xClusterQueryKey
} from '../../redesign/helpers/api';
import { RuntimeConfigKey } from '../../redesign/helpers/constants';
import { getXClusterConfigUuids } from './ReplicationUtils';

import { XClusterConfig } from './dtos';

import styles from './XClusterConfigList.module.scss';

interface Props {
  currentUniverseUUID: string;
}

export function XClusterConfigList({ currentUniverseUUID }: Props) {
  const queryClient = useQueryClient();
  const { t } = useTranslation();
  const customerUUID = localStorage.getItem('customerId') ?? '';
  const customerRuntimeConfigQuery = useQuery(
    runtimeConfigQueryKey.customerScope(customerUUID),
    () => api.fetchRuntimeConfigs(customerUUID, true)
  );

  const universeQuery = useQuery(universeQueryKey.detail(currentUniverseUUID), () =>
    api.fetchUniverse(currentUniverseUUID)
  );

  const { sourceXClusterConfigUuids, targetXClusterConfigUuids } = getXClusterConfigUuids(
    universeQuery.data
  );
  // List the XCluster Configurations for which the current universe is a source or a target.
  const universeXClusterConfigUUIDs: string[] = [
    ...sourceXClusterConfigUuids,
    ...targetXClusterConfigUuids
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
        xClusterConfig.data?.status &&
        _.includes(TRANSITORY_XCLUSTER_CONFIG_STATUSES, xClusterConfig.data.status)
      ) {
        queryClient.invalidateQueries(xClusterQueryKey.detail(xClusterConfig.data.uuid));
      }
    });
  }, XCLUSTER_METRIC_REFETCH_INTERVAL_MS);

  if (universeQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('failedToFetchCurrentUniverse', { keyPrefix: 'queryError' })}
      />
    );
  }
  if (customerRuntimeConfigQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('failedToFetchCustomerRuntimeConfig', { keyPrefix: 'queryError' })}
      />
    );
  }
  if (
    universeQuery.isLoading ||
    universeQuery.isIdle ||
    customerRuntimeConfigQuery.isLoading ||
    customerRuntimeConfigQuery.isIdle
  ) {
    return <YBLoading />;
  }

  const runtimeConfigEntries = customerRuntimeConfigQuery.data.configEntries ?? [];
  const shouldShowDrXClusterConfigs = runtimeConfigEntries.some(
    (config: any) =>
      config.key === RuntimeConfigKey.SHOW_DR_XCLUSTER_CONFIG && config.value === 'true'
  );
  const shownXClusterConfigQueries = shouldShowDrXClusterConfigs
    ? xClusterConfigQueries
    : xClusterConfigQueries.filter((xClusterConfigQuery) => !xClusterConfigQuery.data?.usedForDr);

  return (
    <>
      <ul className={styles.listContainer}>
        {shownXClusterConfigQueries.length === 0 ? (
          <div className={clsx(styles.configCard, styles.emptyConfigListPlaceholder)}>
            No replications to show.
          </div>
        ) : (
          shownXClusterConfigQueries.map((xClusterConfigQuery, index) => {
            const xClusterConfigUUID = universeXClusterConfigUUIDs[index];
            if (xClusterConfigQuery.isLoading) {
              return (
                <li className={clsx(styles.listItem)} key={xClusterConfigUUID}>
                  <div className={clsx(styles.configCard, styles.loading)}>
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

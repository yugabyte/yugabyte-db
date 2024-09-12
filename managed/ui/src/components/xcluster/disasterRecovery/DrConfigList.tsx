import { useState } from 'react';
import { makeStyles, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { useQueries, useQuery, useQueryClient, UseQueryResult } from 'react-query';
import clsx from 'clsx';
import { useInterval } from 'react-use';

import { api, drConfigQueryKey, universeQueryKey } from '../../../redesign/helpers/api';
import { YBErrorIndicator, YBLoading, YBLoadingCircleIcon } from '../../common/indicators';
import { getDrConfigUuids } from '../ReplicationUtils';
import { EnableDrPrompt } from './EnableDrPrompt';
import { CreateConfigModal } from './createConfig/CreateConfigModal';
import { DrConfigCard } from './DrConfigCard';
import { isActionFrozen } from '../../../redesign/helpers/utils';
import { UnavailableUniverseStates, UNIVERSE_TASKS } from '../../../redesign/helpers/constants';
import { RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { YBButton, YBTooltip } from '../../../redesign/components';
import { getUniverseStatus, UniverseState } from '../../universes/helpers/universeHelpers';
import { PollingIntervalMs, TRANSITORY_XCLUSTER_CONFIG_STATUSES } from '../constants';

import { DrConfig } from './dtos';

import styles from './DrConfigList.module.scss';

interface DrConfigListProps {
  currentUniverseUuid: string;
}
const useStyles = makeStyles(() => ({
  header: {
    display: 'flex'
  },
  actionButtonContainer: {
    marginLeft: 'auto'
  }
}));

const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery';

export const DrConfigList = ({ currentUniverseUuid }: DrConfigListProps) => {
  const [isCreateConfigModalOpen, setIsCreateConfigModalOpen] = useState<boolean>(false);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();
  const queryClient = useQueryClient();

  const currentUniverseQuery = useQuery(universeQueryKey.detail(currentUniverseUuid), () =>
    api.fetchUniverse(currentUniverseUuid)
  );

  const { sourceDrConfigUuids, targetDrConfigUuids } = getDrConfigUuids(currentUniverseQuery.data);
  const universeDrConfigUuids = [...sourceDrConfigUuids, ...targetDrConfigUuids];

  // The unsafe cast is needed due to issue with useQueries typing
  // Upgrading react-query to v3.28 may solve this issue: https://github.com/TanStack/query/issues/1675
  const drConfigQueries = useQueries(
    universeDrConfigUuids.map((drConfigUuid: string) => ({
      queryKey: drConfigQueryKey.detail(drConfigUuid),
      queryFn: () => api.fetchDrConfig(drConfigUuid),
      enabled: currentUniverseQuery.data !== undefined
    }))
  ) as UseQueryResult<DrConfig>[];

  // Polling for config updates.
  useInterval(() => {
    if (getUniverseStatus(currentUniverseQuery.data)?.state === UniverseState.PENDING) {
      queryClient.invalidateQueries(
        universeQueryKey.detail(currentUniverseQuery.data?.universeUUID)
      );
    }
  }, PollingIntervalMs.UNIVERSE_STATE_TRANSITIONS);
  useInterval(() => {
    drConfigQueries.forEach((drConfigQuery) => {
      const xClusterConfigStatus = drConfigQuery.data?.status;
      if (
        xClusterConfigStatus !== undefined &&
        TRANSITORY_XCLUSTER_CONFIG_STATUSES.includes(xClusterConfigStatus)
      ) {
        queryClient.invalidateQueries(drConfigQueryKey.detail(drConfigQuery.data?.uuid));
      }
    });
  }, PollingIntervalMs.DR_CONFIG_STATE_TRANSITIONS);
  useInterval(() => {
    drConfigQueries.forEach((drConfigQuery) => {
      queryClient.invalidateQueries(drConfigQueryKey.detail(drConfigQuery.data?.uuid));
    });
  }, PollingIntervalMs.DR_CONFIG);

  if (currentUniverseQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('failedToFetchCurrentUniverse', { keyPrefix: 'queryError' })}
      />
    );
  }

  if (currentUniverseQuery.isLoading || currentUniverseQuery.isIdle) {
    return <YBLoading />;
  }

  const openCreateConfigModal = () => setIsCreateConfigModalOpen(true);
  const closeCreateConfigModal = () => setIsCreateConfigModalOpen(false);

  const allowedTasks = currentUniverseQuery.data?.allowedTasks;
  const shouldDisableCreateDrConfig =
    UnavailableUniverseStates.includes(getUniverseStatus(currentUniverseQuery.data).state) ||
    isActionFrozen(allowedTasks, UNIVERSE_TASKS.CONFIGURE_DR);
  const shouldShowHeaderActionButtons = drConfigQueries.length > 0;
  return (
    <>
      <div className={classes.header}>
        <Typography variant="h3">{t('heading')}</Typography>
        {shouldShowHeaderActionButtons && (
          <div className={classes.actionButtonContainer}>
            <RbacValidator
              accessRequiredOn={{
                onResource: currentUniverseUuid,
                ...ApiPermissionMap.CREATE_DR_CONFIG
              }}
              isControl
            >
              <YBTooltip
                title={
                  shouldDisableCreateDrConfig
                    ? UnavailableUniverseStates.includes(
                        getUniverseStatus(currentUniverseQuery.data).state
                      )
                      ? t('actionButton.createDrConfig.tooltip.universeUnavailable')
                      : ''
                    : ''
                }
                placement="top"
              >
                <span>
                  <YBButton
                    variant="primary"
                    onClick={openCreateConfigModal}
                    disabled={shouldDisableCreateDrConfig}
                    data-testid={'DrConfigList-ConfigureDrButton'}
                  >
                    {t('actionButton.createDrConfig.label')}
                  </YBButton>
                </span>
              </YBTooltip>
            </RbacValidator>
          </div>
        )}
      </div>
      <ul className={styles.listContainer}>
        {drConfigQueries.length === 0 ? (
          <>
            <EnableDrPrompt
              onConfigureDrButtonClick={openCreateConfigModal}
              isDisabled={false}
              universeUUID={currentUniverseUuid}
            />
          </>
        ) : (
          drConfigQueries.map((drConfigQuery, index) => {
            const drConfigUuid = universeDrConfigUuids[index];
            if (drConfigQuery.isLoading) {
              return (
                <li className={clsx(styles.listItem)} key={drConfigUuid}>
                  <div className={clsx(styles.configCard, styles.loading)}>
                    <YBLoadingCircleIcon />
                  </div>
                </li>
              );
            }
            if (drConfigQuery.isError) {
              return (
                <li className={styles.listItem} key={drConfigUuid}>
                  <div className={clsx(styles.configCard, styles.error)}>
                    <i className="fa fa-exclamation-triangle" />
                    <Typography variant="h5">
                      {`Error fetching xCluster DR configuration: ${drConfigUuid}`}
                    </Typography>
                  </div>
                </li>
              );
            }
            if (drConfigQuery.data) {
              return (
                <li className={styles.listItem} key={drConfigQuery.data.uuid}>
                  <DrConfigCard
                    currentUniverseUuid={currentUniverseUuid}
                    drConfig={drConfigQuery.data}
                  />
                </li>
              );
            }
            return null;
          })
        )}
      </ul>
      {isCreateConfigModalOpen && (
        <CreateConfigModal
          sourceUniverseUuid={currentUniverseUuid}
          modalProps={{ open: isCreateConfigModalOpen, onClose: closeCreateConfigModal }}
        />
      )}
    </>
  );
};

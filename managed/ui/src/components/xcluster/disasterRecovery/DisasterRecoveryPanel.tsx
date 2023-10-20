import { useState } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { useQuery, useQueryClient } from 'react-query';
import { makeStyles, Typography } from '@material-ui/core';
import { DropdownButton, MenuItem } from 'react-bootstrap';
import { useInterval } from 'react-use';
import { browserHistory } from 'react-router';

import { CreateConfigModal } from './createConfig/CreateConfigModal';
import { DeleteConfigModal } from './deleteConfig/DeleteConfigModal';
import { DisasterRecoveryConfig } from './drConfig/DisasterRecoveryConfig';
import { DrConfigActions } from './constants';
import { EditTablesModal } from './editTables/EditTablesModal';
import { EnableDrPrompt } from './EnableDrPrompt';
import { InitiateFailoverModal } from './failover/InitiateFailoverModal';
import {
  PollingIntervalMs,
  TRANSITORY_XCLUSTER_CONFIG_STATUSES,
  XClusterConfigAction
} from '../constants';
import { YBButton } from '../../../redesign/components';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { YBBanner, YBBannerVariant, YBLabelWithIcon } from '../../common/descriptors';
import { api, drConfigQueryKey, universeQueryKey } from '../../../redesign/helpers/api';
import { getEnabledConfigActions } from '../ReplicationUtils';
import { getEnabledDrConfigActions } from './utils';
import { EditConfigModal } from './editConfig/EditConfigModal';
import { RestartConfigModal } from '../restartConfig/RestartConfigModal';
import { EditConfigTargetModal } from './editConfigTarget/EditConfigTargetModal';

import { TableType } from '../../../redesign/helpers/dtos';

interface DisasterRecoveryProps {
  currentUniverseUuid: string;
}

const useStyles = makeStyles((theme) => ({
  bannerContainer: {
    // Banners are hidden until the logic for showing and hiding
    // them gets added.
    display: 'none',
    flexDirection: 'column',
    gap: theme.spacing(1)
  },
  bannerContent: {
    display: 'flex',
    alignItems: 'center'
  },
  bannerActionButtonContainer: {
    display: 'flex',
    gap: theme.spacing(1),

    marginLeft: 'auto'
  },
  header: {
    display: 'flex',
    alignItems: 'center',

    margin: `${theme.spacing(3)}px 0 ${theme.spacing(2)}px`,

    '& $actionButtonContainer': {
      marginLeft: 'auto'
    }
  },
  actionButtonContainer: {
    display: 'flex',
    gap: theme.spacing(1)
  },
  labelContainer: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1),

    '& i': {
      margin: 0
    }
  },
  enableDrContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(3),

    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: '8px'
  }
}));

const PANEL_ID = 'DisasterRecovery';
const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery';

export const DisasterRecovery = ({ currentUniverseUuid }: DisasterRecoveryProps) => {
  const [isCreateConfigModalOpen, setIsCreateConfigModalOpen] = useState<boolean>(false);
  const [isDeleteConfigModalOpen, setIsDeleteConfigModalOpen] = useState<boolean>(false);
  const [isEditConfigModalOpen, setIsEditConfigModalOpen] = useState<boolean>(false);
  const [isEditConfigTargetModalOpen, setIsEditTargetConfigModalOpen] = useState<boolean>(false);
  const [isEditTablesModalOpen, setIsEditTablesModalOpen] = useState<boolean>(false);
  const [isInitiateFailoverModalOpen, setIsInitiateFailoverModalOpen] = useState<boolean>(false);
  const [isRestartConfigModalOpen, setIsRestartConfigModalOpen] = useState<boolean>(false);
  const classes = useStyles();
  const queryClient = useQueryClient();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const currentUniverseQuery = useQuery(universeQueryKey.detail(currentUniverseUuid), () =>
    api.fetchUniverse(currentUniverseUuid)
  );

  // Currently, each universe only supports a single DR config.
  // When we start allowing more than one DR config per universe, we will update this section of the UI.
  const drConfigUuid =
    currentUniverseQuery.data?.drConfigUuidsAsSource[0] ??
    currentUniverseQuery.data?.drConfigUuidsAsTarget[0];
  const drConfigQuery = useQuery(
    drConfigQueryKey.detail(drConfigUuid),
    () => api.fetchDrConfig(drConfigUuid),
    { enabled: !!drConfigUuid }
  );

  // Polling for metrics and config updates.
  useInterval(() => {
    queryClient.invalidateQueries('xcluster-metric'); // TODO: Add a dedicated key for 'latest xCluster metrics'.
  }, PollingIntervalMs.XCLUSTER_METRICS);
  useInterval(() => {
    const xClusterConfig = drConfigQuery.data?.xClusterConfig;
    if (
      xClusterConfig !== undefined &&
      TRANSITORY_XCLUSTER_CONFIG_STATUSES.includes(xClusterConfig.status)
    ) {
      queryClient.invalidateQueries(drConfigQueryKey.detail(drConfigUuid));
    }
  }, PollingIntervalMs.DR_CONFIG_STATE_TRANSITIONS);
  useInterval(() => {
    queryClient.invalidateQueries(drConfigQueryKey.detail(drConfigUuid));
  }, PollingIntervalMs.DR_CONFIG);

  const { sourceUniverseUUID: sourceUniverseUuid, targetUniverseUUID: targetUniverseUuid } =
    drConfigQuery.data?.xClusterConfig ?? {};
  // For DR, the currentUniverseUuid is not guaranteed to be the sourceUniverseUuid.
  const participantUniveresUuid =
    currentUniverseUuid !== targetUniverseUuid ? targetUniverseUuid : sourceUniverseUuid;
  const participantUniverseQuery = useQuery(
    universeQueryKey.detail(participantUniveresUuid),
    () => api.fetchUniverse(participantUniveresUuid),
    { enabled: !!participantUniveresUuid }
  );
  if (
    currentUniverseQuery.isLoading ||
    drConfigQuery.isLoading ||
    participantUniverseQuery.isLoading
  ) {
    return <YBLoading />;
  }
  if (currentUniverseQuery.isError || participantUniverseQuery.isError) {
    return <YBErrorIndicator customErrorMessage={t('error.failToFetchUniverse')} />;
  }
  if (drConfigQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('error.failToFetchDrConfig', { drConfigUuid: drConfigUuid })}
      />
    );
  }

  const drConfig = drConfigQuery.data;
  const openCreateConfigModal = () => setIsCreateConfigModalOpen(true);
  const closeCreateConfigModal = () => setIsCreateConfigModalOpen(false);
  if (!drConfig) {
    return (
      <>
        <div className={classes.header}>
          <Typography variant="h3">{t('heading')}</Typography>
        </div>
        <EnableDrPrompt onConfigureDrButtonClick={openCreateConfigModal} />
        <CreateConfigModal
          onHide={closeCreateConfigModal}
          visible={isCreateConfigModalOpen}
          sourceUniverseUuid={currentUniverseUuid}
        />
      </>
    );
  }

  const openInitiateFailoverModal = () => setIsInitiateFailoverModalOpen(true);
  const closeInitiateFailoverModal = () => setIsInitiateFailoverModalOpen(false);
  const openDeleteConfigModal = () => setIsDeleteConfigModalOpen(true);
  const closeDeleteConfigModal = () => setIsDeleteConfigModalOpen(false);
  const openEditTablesModal = () => setIsEditTablesModalOpen(true);
  const closeEditTablesModal = () => setIsEditTablesModalOpen(false);
  const openEditConfigModal = () => setIsEditConfigModalOpen(true);
  const closeEditConfigModal = () => setIsEditConfigModalOpen(false);
  const openEditTargetConfigModal = () => setIsEditTargetConfigModalOpen(true);
  const closeEditTargetConfigModal = () => setIsEditTargetConfigModalOpen(false);
  const openRestartConfigModal = () => setIsRestartConfigModalOpen(true);
  const closeRestartConfigModal = () => setIsRestartConfigModalOpen(false);

  const handleMoreActionMenuClick = (eventKey: unknown) => {
    switch (eventKey) {
      case DrConfigActions.EDIT:
        openEditConfigModal();
        return;
      case DrConfigActions.EDIT_TARGET:
        openEditTargetConfigModal();
        return;
      case DrConfigActions.DELETE:
        openDeleteConfigModal();
        return;
      case XClusterConfigAction.MANAGE_TABLE:
        openEditTablesModal();
    }
  };

  const [sourceUniverse, targetUniverse] =
    currentUniverseUuid !== targetUniverseUuid
      ? [currentUniverseQuery.data, participantUniverseQuery.data]
      : [participantUniverseQuery.data, currentUniverseQuery.data];
  const enabledDrConfigActions = getEnabledDrConfigActions(
    drConfig,
    sourceUniverse,
    targetUniverse
  );
  const enabledXClusterConfigActions = getEnabledConfigActions(
    drConfig.xClusterConfig,
    sourceUniverse,
    targetUniverse
  );

  return (
    <>
      <div className={classes.bannerContainer}>
        {/* TODO: Add the rest of the banners and conditional logic on dr/xCluster status. */}
        <YBBanner
          variant={YBBannerVariant.INFO}
          bannerIcon={<i className="fa fa-spinner fa-spin" />}
          isFeatureBanner={true}
        >
          <div className={classes.bannerContent}>
            <Typography variant="body2">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX}.banner.enablingDr`}
                components={{ bold: <b /> }}
              />
            </Typography>
            <div className={classes.bannerActionButtonContainer}>
              <YBButton
                variant="secondary"
                size="large"
                onClick={() => browserHistory.push(`/universes/${sourceUniverseUuid}/tasks`)}
              >
                {t('actionButton.viewTaskDetails')}
              </YBButton>
            </div>
          </div>
        </YBBanner>
        <YBBanner
          variant={YBBannerVariant.INFO}
          bannerIcon={<i className="fa fa-spinner fa-spin" />}
          isFeatureBanner={true}
        >
          <div className={classes.bannerContent}>
            <Typography variant="body2">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX}.banner.failoverInProgress`}
                components={{ bold: <b /> }}
              />
            </Typography>
            <div className={classes.bannerActionButtonContainer}>
              <YBButton variant="secondary" size="large" onClick={() => {}}>
                {t('actionButton.abortFailover')}
              </YBButton>
              <YBButton
                variant="secondary"
                size="large"
                onClick={() => browserHistory.push(`/universes/${sourceUniverseUuid}/tasks`)}
              >
                {t('actionButton.viewTaskDetails')}
              </YBButton>
            </div>
          </div>
        </YBBanner>
        <YBBanner
          variant={YBBannerVariant.INFO}
          bannerIcon={<i className="fa fa-flag" />}
          isFeatureBanner={true}
        >
          <div className={classes.bannerContent}>
            <Typography variant="body2">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX}.banner.connectYourApps`}
                components={{ bold: <b /> }}
              />
            </Typography>
            <div className={classes.bannerActionButtonContainer}>
              <YBButton variant="secondary" size="large" onClick={() => {}}>
                {t('actionButton.connectApplications')}
              </YBButton>
            </div>
          </div>
        </YBBanner>
        <YBBanner variant={YBBannerVariant.WARNING} isFeatureBanner={true}>
          <div className={classes.bannerContent}>
            <Typography variant="body2">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX}.banner.drConfigHalted`}
                components={{ bold: <b /> }}
              />
            </Typography>
            <div className={classes.bannerActionButtonContainer}>
              <YBButton variant="secondary" size="large" onClick={() => {}}>
                {t('actionButton.repairDr')}
              </YBButton>
            </div>
          </div>
        </YBBanner>
        <YBBanner variant={YBBannerVariant.DANGER} isFeatureBanner={true}>
          <div className={classes.bannerContent}>
            <Typography variant="body2">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX}.banner.drConfigRestartRequired`}
                components={{ bold: <b /> }}
              />
            </Typography>
            <div className={classes.bannerActionButtonContainer}>
              <YBButton variant="secondary" size="large" onClick={openRestartConfigModal}>
                {t('actionButton.restartReplication')}
              </YBButton>
            </div>
          </div>
        </YBBanner>
      </div>
      <div className={classes.header}>
        <Typography variant="h3">{t('heading')}</Typography>
        <div className={classes.actionButtonContainer}>
          <YBButton
            variant="primary"
            size="large"
            type="button"
            onClick={openInitiateFailoverModal}
            data-testid={`${PANEL_ID}-initiateFailover`}
          >
            {t('actionButton.initiateFailover')}
          </YBButton>
          <DropdownButton
            bsClass="dropdown"
            title={t('actionButton.actionMenu.label')}
            onSelect={handleMoreActionMenuClick}
            id={`${PANEL_ID}-actionsDropdown`}
            data-testid={`${PANEL_ID}-actionsDropdown`}
            pullRight
          >
            <MenuItem
              eventKey={XClusterConfigAction.MANAGE_TABLE}
              disabled={!enabledXClusterConfigActions.includes(XClusterConfigAction.MANAGE_TABLE)}
            >
              <YBLabelWithIcon icon="fa fa-table">
                {t('actionButton.actionMenu.editTables')}
              </YBLabelWithIcon>
            </MenuItem>
            <MenuItem
              eventKey={DrConfigActions.EDIT}
              disabled={!enabledDrConfigActions.includes(DrConfigActions.EDIT)}
            >
              <YBLabelWithIcon icon="fa fa-cog">
                {t('actionButton.actionMenu.editDrConfig')}
              </YBLabelWithIcon>
            </MenuItem>
            <MenuItem
              eventKey={DrConfigActions.EDIT_TARGET}
              disabled={!enabledDrConfigActions.includes(DrConfigActions.EDIT_TARGET)}
            >
              <YBLabelWithIcon icon="fa fa-globe">
                {t('actionButton.actionMenu.editDrConfigTarget')}
              </YBLabelWithIcon>
            </MenuItem>
            <MenuItem
              eventKey={DrConfigActions.DELETE}
              disabled={!enabledDrConfigActions.includes(DrConfigActions.DELETE)}
            >
              <YBLabelWithIcon icon="fa fa-trash">
                {t('actionButton.actionMenu.deleteConfig')}
              </YBLabelWithIcon>
            </MenuItem>
          </DropdownButton>
        </div>
      </div>
      <div>
        <DisasterRecoveryConfig drConfig={drConfig} />
      </div>
      {isInitiateFailoverModalOpen && (
        <InitiateFailoverModal
          drConfig={drConfig}
          modalProps={{ open: isInitiateFailoverModalOpen, onClose: closeInitiateFailoverModal }}
        />
      )}
      {isDeleteConfigModalOpen && (
        <DeleteConfigModal
          drConfig={drConfig}
          modalProps={{ open: isDeleteConfigModalOpen, onClose: closeDeleteConfigModal }}
        />
      )}
      {isEditConfigModalOpen && (
        <EditConfigModal
          drConfig={drConfig}
          modalProps={{ open: isEditConfigModalOpen, onClose: closeEditConfigModal }}
        />
      )}
      {isEditConfigTargetModalOpen && (
        <EditConfigTargetModal
          drConfig={drConfig}
          modalProps={{ open: isEditConfigTargetModalOpen, onClose: closeEditTargetConfigModal }}
        />
      )}
      {isEditTablesModalOpen && (
        <EditTablesModal
          xClusterConfig={drConfig.xClusterConfig}
          isDrConfig={true}
          modalProps={{ open: isEditTablesModalOpen, onClose: closeEditTablesModal }}
        />
      )}
      {isRestartConfigModalOpen && (
        <RestartConfigModal
          configTableType={TableType.PGSQL_TABLE_TYPE}
          isVisible={isRestartConfigModalOpen}
          onHide={closeRestartConfigModal}
          xClusterConfig={drConfig.xClusterConfig}
        />
      )}
    </>
  );
};

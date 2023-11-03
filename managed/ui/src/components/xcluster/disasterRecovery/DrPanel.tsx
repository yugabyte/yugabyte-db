import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useQuery, useQueryClient } from 'react-query';
import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { DropdownButton, MenuItem } from 'react-bootstrap';
import { useInterval } from 'react-use';

import { CreateConfigModal } from './createConfig/CreateConfigModal';
import { DeleteConfigModal } from './deleteConfig/DeleteConfigModal';
import { DrConfigDetails } from './drConfig/DrConfigDetails';
import { DrConfigActions } from './constants';
import { EditTablesModal } from './editTables/EditTablesModal';
import { EnableDrPrompt } from './EnableDrPrompt';
import {
  PollingIntervalMs,
  TRANSITORY_XCLUSTER_CONFIG_STATUSES,
  XClusterConfigAction
} from '../constants';
import { YBButton } from '../../../redesign/components';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { api, drConfigQueryKey, universeQueryKey } from '../../../redesign/helpers/api';
import { getEnabledConfigActions } from '../ReplicationUtils';
import { getEnabledDrConfigActions } from './utils';
import { EditConfigModal } from './editConfig/EditConfigModal';
import { RestartConfigModal } from '../restartConfig/RestartConfigModal';
import { EditConfigTargetModal } from './editConfigTarget/EditConfigTargetModal';
import { InitiateSwitchoverModal } from './switchover/InitiateSwitchoverModal';
import { SyncXClusterConfigModal } from '../configDetails/SyncXClusterModal';
import { InitiateFailoverModal } from './failover/InitiateFailoverModal';
import { MenuItemsContainer } from '../../universes/UniverseDetail/compounds/MenuItemsContainer';
import { YBMenuItemLabel } from '../../../redesign/components/YBDropdownMenu/YBMenuItemLabel';
import { SwitchoverIcon } from '../icons/SwitchoverIcon';
import { FailoverIcon } from '../icons/FailoverIcon';
import { RepairDrConfigModal } from './repairConfig/RepairDrConfigModal';
import { DrConfigOverview } from './drConfig/DrConfigOverview';
import { DrBannerSection } from './DrBannerSection';

import { TableType } from '../../../redesign/helpers/dtos';

interface DrPanelProps {
  currentUniverseUuid: string;
}

const useStyles = makeStyles((theme) => ({
  bannerContainer: {
    display: 'flex',
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

const ActionMenu = {
  ADVANCED: 'advanced'
} as const;
const PANEL_ID = 'DisasterRecovery';
const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery';

export const DrPanel = ({ currentUniverseUuid }: DrPanelProps) => {
  const [isActionMenuOpen, setIsActionMenuOpen] = useState<boolean>(false);
  const [isSwitchoverModalOpen, setIsSwitchoverModalOpen] = useState<boolean>(false);
  const [isFailoverModalOpen, setIsFailoverModalOpen] = useState<boolean>(false);
  const [isCreateConfigModalOpen, setIsCreateConfigModalOpen] = useState<boolean>(false);
  const [isDeleteConfigModalOpen, setIsDeleteConfigModalOpen] = useState<boolean>(false);
  const [isEditConfigModalOpen, setIsEditConfigModalOpen] = useState<boolean>(false);
  const [isEditConfigTargetModalOpen, setIsEditTargetConfigModalOpen] = useState<boolean>(false);
  const [isEditTablesModalOpen, setIsEditTablesModalOpen] = useState<boolean>(false);
  const [isRepairConfigModalOpen, setIsRepairConfigModalOpen] = useState<boolean>(false);
  const [isRestartConfigModalOpen, setIsRestartConfigModalOpen] = useState<boolean>(false);
  const [isDbSyncModalOpen, setIsDbSyncModalOpen] = useState<boolean>(false);
  const classes = useStyles();
  const theme = useTheme();
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
    { enabled: !!drConfigUuid, staleTime: PollingIntervalMs.DR_CONFIG }
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
  if (
    currentUniverseQuery.isLoading ||
    drConfigQuery.isLoading ||
    participantUniverseQuery.isLoading
  ) {
    return <YBLoading />;
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

  if (!sourceUniverseUuid || !targetUniverseUuid) {
    const errorMessageKey = sourceUniverseUuid
      ? 'undefinedTargetUniverseUuid'
      : 'undefinedSourceUniverseUuid';
    return (
      <YBErrorIndicator
        customErrorMessage={t(`error.${errorMessageKey}`, {
          keyPrefix: 'clusterDetail.xCluster'
        })}
      />
    );
  }

  const openSwitchoverModal = () => setIsSwitchoverModalOpen(true);
  const closeSwitchoverModal = () => setIsSwitchoverModalOpen(false);
  const openFailoverModal = () => setIsFailoverModalOpen(true);
  const closeFailoverModal = () => setIsFailoverModalOpen(false);
  const openDeleteConfigModal = () => setIsDeleteConfigModalOpen(true);
  const closeDeleteConfigModal = () => setIsDeleteConfigModalOpen(false);
  const openEditTablesModal = () => setIsEditTablesModalOpen(true);
  const closeEditTablesModal = () => setIsEditTablesModalOpen(false);
  const openEditConfigModal = () => setIsEditConfigModalOpen(true);
  const closeEditConfigModal = () => setIsEditConfigModalOpen(false);
  const openEditTargetConfigModal = () => setIsEditTargetConfigModalOpen(true);
  const closeEditTargetConfigModal = () => setIsEditTargetConfigModalOpen(false);
  const openRepairConfigModal = () => setIsRepairConfigModalOpen(true);
  const closeRepairConfigModal = () => setIsRepairConfigModalOpen(false);
  const openRestartConfigModal = () => setIsRestartConfigModalOpen(true);
  const closeRestartConfigModal = () => setIsRestartConfigModalOpen(false);
  const openDbSyncModal = () => setIsDbSyncModalOpen(true);
  const closeDbSyncModal = () => setIsDbSyncModalOpen(false);

  const handleActionMenuToggle = (isOpen: boolean) => {
    setIsActionMenuOpen(isOpen);
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
      <DrBannerSection
        drConfig={drConfig}
        openRepairConfigModal={openRepairConfigModal}
        openRestartConfigModal={openRestartConfigModal}
      />
      <div className={classes.header}>
        <Typography variant="h3">{t('heading')}</Typography>
        <div className={classes.actionButtonContainer}>
          <YBButton
            variant="primary"
            size="large"
            type="button"
            onClick={openSwitchoverModal}
            disabled={!enabledDrConfigActions.includes(DrConfigActions.SWITCHOVER)}
            data-testid={`${PANEL_ID}-switchover`}
          >
            {t('actionButton.switchover')}
          </YBButton>
          <DropdownButton
            bsClass="dropdown"
            title={t('actionButton.actionMenu.label')}
            onToggle={handleActionMenuToggle}
            id={`${PANEL_ID}-actionsDropdown`}
            data-testid={`${PANEL_ID}-actionsDropdown`}
            pullRight
          >
            <MenuItemsContainer
              parentDropdownOpen={isActionMenuOpen}
              mainMenu={(showSubmenu) => (
                <>
                  <MenuItem
                    eventKey={XClusterConfigAction.MANAGE_TABLE}
                    onSelect={openEditTablesModal}
                    disabled={
                      !enabledXClusterConfigActions.includes(XClusterConfigAction.MANAGE_TABLE)
                    }
                  >
                    <YBMenuItemLabel
                      label={t('actionButton.actionMenu.editTables')}
                      preLabelElement={<i className="fa fa-table" />}
                    />
                  </MenuItem>
                  <MenuItem
                    eventKey={DrConfigActions.EDIT}
                    onSelect={openEditConfigModal}
                    disabled={!enabledDrConfigActions.includes(DrConfigActions.EDIT)}
                  >
                    <YBMenuItemLabel
                      label={t('actionButton.actionMenu.editDrConfig')}
                      preLabelElement={<i className="fa fa-cog" />}
                    />
                  </MenuItem>
                  <MenuItem
                    eventKey={DrConfigActions.EDIT_TARGET}
                    onSelect={openEditTargetConfigModal}
                    disabled={!enabledDrConfigActions.includes(DrConfigActions.EDIT_TARGET)}
                  >
                    <YBMenuItemLabel
                      label={t('actionButton.actionMenu.editDrConfigTarget')}
                      preLabelElement={<i className="fa fa-globe" />}
                    />
                  </MenuItem>
                  <MenuItem
                    eventKey={ActionMenu.ADVANCED}
                    onSelect={() => showSubmenu(ActionMenu.ADVANCED)}
                  >
                    <YBMenuItemLabel
                      label={t('actionButton.actionMenu.advancedSubmenu')}
                      preLabelElement={<i className="fa fa-cogs" />}
                      postLabelElement={
                        <Box component="span" marginLeft="auto">
                          <i className="fa fa-chevron-right" />
                        </Box>
                      }
                    />
                  </MenuItem>
                  <MenuItem divider />
                  <MenuItem
                    eventKey={DrConfigActions.SWITCHOVER}
                    onSelect={openSwitchoverModal}
                    disabled={!enabledDrConfigActions.includes(DrConfigActions.SWITCHOVER)}
                  >
                    <YBMenuItemLabel
                      label={t('actionButton.actionMenu.switchover')}
                      preLabelElement={
                        <SwitchoverIcon
                          isDisabled={!enabledDrConfigActions.includes(DrConfigActions.SWITCHOVER)}
                        />
                      }
                    />
                  </MenuItem>
                  <MenuItem
                    eventKey={DrConfigActions.FAILOVER}
                    onSelect={openFailoverModal}
                    disabled={!enabledDrConfigActions.includes(DrConfigActions.FAILOVER)}
                  >
                    <YBMenuItemLabel
                      label={t('actionButton.actionMenu.failover')}
                      preLabelElement={
                        <FailoverIcon
                          isDisabled={!enabledDrConfigActions.includes(DrConfigActions.FAILOVER)}
                        />
                      }
                    />
                  </MenuItem>
                  <MenuItem divider />
                  <MenuItem
                    eventKey={DrConfigActions.DELETE}
                    onSelect={openDeleteConfigModal}
                    disabled={!enabledDrConfigActions.includes(DrConfigActions.DELETE)}
                  >
                    <YBMenuItemLabel
                      label={t('actionButton.actionMenu.deleteConfig')}
                      preLabelElement={<i className="fa fa-trash" />}
                    />
                  </MenuItem>
                </>
              )}
              subMenus={{
                // eslint-disable-next-line react/display-name
                [ActionMenu.ADVANCED]: (navigateToMainMenu) => (
                  <>
                    <MenuItem eventKey="back" onSelect={navigateToMainMenu}>
                      <YBMenuItemLabel
                        label={t('back', { keyPrefix: 'common' })}
                        preLabelElement={<i className="fa fa-chevron-left fa-fw" />}
                      />
                    </MenuItem>
                    <MenuItem
                      eventKey={XClusterConfigAction.RESTART}
                      onSelect={openRestartConfigModal}
                      disabled={
                        !enabledXClusterConfigActions.includes(XClusterConfigAction.RESTART)
                      }
                    >
                      <YBMenuItemLabel
                        label={t('actionButton.actionMenu.restartReplication')}
                        preLabelElement={<i className="fa fa-refresh" />}
                      />
                    </MenuItem>
                    <MenuItem
                      eventKey={XClusterConfigAction.DB_SYNC}
                      onSelect={openDbSyncModal}
                      disabled={
                        !enabledXClusterConfigActions.includes(XClusterConfigAction.DB_SYNC)
                      }
                    >
                      <YBMenuItemLabel
                        label={t('actionButton.actionMenu.dbSync')}
                        preLabelElement={<i className="fa fa-refresh" />}
                      />
                    </MenuItem>
                  </>
                )
              }}
            />
          </DropdownButton>
        </div>
      </div>
      <Box display="flex" flexDirection="column" gridGap={theme.spacing(3)}>
        <DrConfigOverview drConfig={drConfig} />
        <DrConfigDetails drConfig={drConfig} />
      </Box>
      {isSwitchoverModalOpen && (
        <InitiateSwitchoverModal
          drConfig={drConfig}
          modalProps={{ open: isSwitchoverModalOpen, onClose: closeSwitchoverModal }}
        />
      )}
      {isFailoverModalOpen && (
        <InitiateFailoverModal
          drConfig={drConfig}
          modalProps={{ open: isFailoverModalOpen, onClose: closeFailoverModal }}
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
      {isRepairConfigModalOpen && (
        <RepairDrConfigModal
          drConfig={drConfig}
          modalProps={{ open: isRepairConfigModalOpen, onClose: closeRepairConfigModal }}
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
      {isDbSyncModalOpen && (
        <SyncXClusterConfigModal
          xClusterConfig={drConfig.xClusterConfig}
          modalProps={{ open: isDbSyncModalOpen, onClose: closeDbSyncModal }}
        />
      )}
    </>
  );
};

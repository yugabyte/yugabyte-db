import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useQuery, useQueryClient } from 'react-query';
import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { DropdownButton, MenuItem } from 'react-bootstrap';
import { useInterval } from 'react-use';
import { Link } from 'react-router';

import { DeleteConfigModal } from './deleteConfig/DeleteConfigModal';
import { DrConfigDetails } from './drConfig/DrConfigDetails';
import { DrConfigAction } from './constants';
import { EditTablesModal } from './editTables/EditTablesModal';
import {
  PollingIntervalMs,
  TRANSITORY_XCLUSTER_CONFIG_STATUSES,
  XClusterConfigAction,
  XClusterConfigType
} from '../constants';
import { YBButton } from '../../../redesign/components';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import {
  api,
  drConfigQueryKey,
  metricQueryKey,
  universeQueryKey,
  xClusterQueryKey
} from '../../../redesign/helpers/api';
import {
  getEnabledConfigActions,
  getInConfigTableUuid,
  getIsXClusterConfigAllBidirectional
} from '../ReplicationUtils';
import { getEnabledDrConfigActions, getXClusterConfig } from './utils';
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
import {
  hasNecessaryPerm,
  RbacValidator
} from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { getUniverseStatus, UniverseState } from '../../universes/helpers/universeHelpers';
import { EditConfigModal } from './editConfig/EditConfigModal';
import { isBootstrapRequired } from '../../../actions/xClusterReplication';

interface DrPanelProps {
  currentUniverseUuid: string;
  drConfigUuid: string;
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

    marginBottom: theme.spacing(2),

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

export const DrPanel = ({ currentUniverseUuid, drConfigUuid }: DrPanelProps) => {
  const [isActionMenuOpen, setIsActionMenuOpen] = useState<boolean>(false);
  const [isSwitchoverModalOpen, setIsSwitchoverModalOpen] = useState<boolean>(false);
  const [isFailoverModalOpen, setIsFailoverModalOpen] = useState<boolean>(false);
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

  const drConfigQuery = useQuery(
    drConfigQueryKey.detail(drConfigUuid, false /* syncWithDb */),
    () => api.fetchDrConfig(drConfigUuid, false /* syncWithDb */),
    { enabled: !!drConfigUuid, staleTime: PollingIntervalMs.DR_CONFIG }
  );
  const drConfigFullQuery = useQuery(
    drConfigQueryKey.detail(drConfigUuid),
    () => api.fetchDrConfig(drConfigUuid),
    { enabled: !!drConfigUuid, staleTime: PollingIntervalMs.DR_CONFIG }
  );

  const { primaryUniverseUuid: sourceUniverseUuid, drReplicaUniverseUuid: targetUniverseUuid } =
    drConfigQuery.data ?? {};
  // For DR, the currentUniverseUuid is not guaranteed to be the sourceUniverseUuid.
  const participantUniverseUuid =
    currentUniverseUuid !== targetUniverseUuid ? targetUniverseUuid : sourceUniverseUuid;
  const participantUniverseQuery = useQuery(
    universeQueryKey.detail(participantUniverseUuid),
    () => api.fetchUniverse(participantUniverseUuid),
    { enabled: !!participantUniverseUuid }
  );

  const [sourceUniverse, targetUniverse] =
    currentUniverseUuid !== targetUniverseUuid
      ? [currentUniverseQuery.data, participantUniverseQuery.data]
      : [participantUniverseQuery.data, currentUniverseQuery.data];

  const inConfigTableUuids = getInConfigTableUuid(drConfigQuery.data?.tableDetails ?? []);
  const bootstrapRequirementQuery = useQuery(
    xClusterQueryKey.needBootstrap({
      sourceUniverseUuid: drConfigQuery.data?.primaryUniverseUuid,
      targetUniverseUuid: drConfigQuery.data?.drReplicaUniverseUuid,
      tableUuids: inConfigTableUuids,
      configType: drConfigQuery.data?.type,
      includeDetails: true
    }),
    () =>
      isBootstrapRequired(
        drConfigQuery.data?.primaryUniverseUuid ?? '',
        drConfigQuery.data?.drReplicaUniverseUuid ?? '',
        inConfigTableUuids,
        drConfigQuery.data?.type ?? XClusterConfigType.TXN,
        true
      ),
    { enabled: !!drConfigQuery.data }
  );
  // Polling for live metrics and config updates.
  useInterval(() => {
    if (getUniverseStatus(sourceUniverse)?.state === UniverseState.PENDING) {
      queryClient.invalidateQueries(universeQueryKey.detail(sourceUniverse?.universeUUID));
    }
    if (getUniverseStatus(targetUniverse)?.state === UniverseState.PENDING) {
      queryClient.invalidateQueries(universeQueryKey.detail(targetUniverse?.universeUUID));
    }
  }, PollingIntervalMs.UNIVERSE_STATE_TRANSITIONS);
  useInterval(() => {
    queryClient.invalidateQueries(metricQueryKey.live());
  }, PollingIntervalMs.XCLUSTER_METRICS);
  useInterval(() => {
    const xClusterConfigStatus = drConfigQuery.data?.status;
    if (
      xClusterConfigStatus !== undefined &&
      TRANSITORY_XCLUSTER_CONFIG_STATUSES.includes(xClusterConfigStatus)
    ) {
      queryClient.invalidateQueries(drConfigQueryKey.detail(drConfigUuid));
    }
  }, PollingIntervalMs.DR_CONFIG_STATE_TRANSITIONS);
  useInterval(() => {
    queryClient.invalidateQueries(drConfigQueryKey.detail(drConfigUuid));
  }, PollingIntervalMs.DR_CONFIG);

  if (currentUniverseQuery.isError || participantUniverseQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('error.failToFetchUniverse', {
          universeUuid: currentUniverseQuery.isError ? currentUniverseUuid : participantUniverseUuid
        })}
      />
    );
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
    currentUniverseQuery.isIdle ||
    drConfigQuery.isLoading ||
    drConfigQuery.isIdle ||
    participantUniverseQuery.isLoading ||
    participantUniverseQuery.isIdle
  ) {
    return <YBLoading />;
  }

  const allowedTasks = currentUniverseQuery.data?.allowedTasks;
  const openDeleteConfigModal = () => setIsDeleteConfigModalOpen(true);
  const closeDeleteConfigModal = () => setIsDeleteConfigModalOpen(false);

  if (!sourceUniverseUuid || !targetUniverseUuid) {
    const errorMessageKey = sourceUniverseUuid
      ? 'undefinedTargetUniverseUuid'
      : 'undefinedSourceUniverseUuid';
    const remainingUniverseUUID = sourceUniverseUuid ?? targetUniverseUuid;
    const redirectUrl = remainingUniverseUUID
      ? `/universes/${remainingUniverseUUID}/recovery`
      : '/universes';

    return (
      <div>
        <YBErrorIndicator
          customErrorMessage={t(`error.${errorMessageKey}`, {
            keyPrefix: 'clusterDetail.xCluster'
          })}
        />
        <RbacValidator
          customValidateFunction={() => {
            return (
              hasNecessaryPerm({
                ...ApiPermissionMap.DELETE_DR_CONFIG,
                onResource: xClusterConfig.sourceUniverseUUID
              }) &&
              hasNecessaryPerm({
                ...ApiPermissionMap.DELETE_DR_CONFIG,
                onResource: xClusterConfig.targetUniverseUUID
              })
            );
          }}
          isControl
        >
          <YBButton
            variant="primary"
            size="large"
            type="button"
            onClick={openDeleteConfigModal}
            data-testid={`${PANEL_ID}-delete`}
          >
            {t('actionButton.actionMenu.deleteConfig')}
          </YBButton>
        </RbacValidator>
        {isDeleteConfigModalOpen && (
          <DeleteConfigModal
            drConfig={drConfigQuery.data}
            allowedTasks={allowedTasks}
            currentUniverseName={currentUniverseQuery.data.name}
            redirectUrl={redirectUrl}
            modalProps={{ open: isDeleteConfigModalOpen, onClose: closeDeleteConfigModal }}
          />
        )}
      </div>
    );
  }

  const openSwitchoverModal = () => setIsSwitchoverModalOpen(true);
  const closeSwitchoverModal = () => setIsSwitchoverModalOpen(false);
  const openFailoverModal = () => setIsFailoverModalOpen(true);
  const closeFailoverModal = () => setIsFailoverModalOpen(false);
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

  const drConfig = drConfigFullQuery.data ?? drConfigQuery.data;
  const enabledDrConfigActions = getEnabledDrConfigActions(
    drConfig,
    sourceUniverse,
    targetUniverse
  );

  const isXClusterConfigAllBidirectional = bootstrapRequirementQuery.data
    ? getIsXClusterConfigAllBidirectional(bootstrapRequirementQuery.data)
    : false;
  const xClusterConfig = getXClusterConfig(drConfig);
  const deleteConfigRedirectUrl = `/universes/${currentUniverseUuid}/recovery`;
  const enabledXClusterConfigActions = getEnabledConfigActions(
    xClusterConfig,
    sourceUniverse,
    targetUniverse,
    isXClusterConfigAllBidirectional,
    drConfig.state
  );
  return (
    <>
      <RbacValidator
        customValidateFunction={() => {
          return (
            hasNecessaryPerm({
              ...ApiPermissionMap.GET_DR_CONFIG,
              onResource: xClusterConfig.sourceUniverseUUID
            }) &&
            hasNecessaryPerm({
              ...ApiPermissionMap.GET_DR_CONFIG,
              onResource: xClusterConfig.targetUniverseUUID
            })
          );
        }}
      >
        <h2 className="content-title">
          <Link to={`/universes/${xClusterConfig.sourceUniverseUUID}`}>
            {sourceUniverse?.name ?? 'Undefined Universe'}
          </Link>
          <span className="subtext">
            <i className="fa fa-chevron-right submenu-icon" />
            <Link to={`/universes/${xClusterConfig.sourceUniverseUUID}/recovery/`}>
              xCluster Disaster Recovery
            </Link>
            <i className="fa fa-chevron-right submenu-icon" />
            {drConfig.name}
          </span>
        </h2>
        <Box padding={3}>
          <DrBannerSection
            drConfig={drConfig}
            openRepairConfigModal={openRepairConfigModal}
            openRestartConfigModal={openRestartConfigModal}
            enabledDrConfigActions={enabledDrConfigActions}
            enabledXClusterConfigActions={enabledXClusterConfigActions}
          />
          <div className={classes.header}>
            <Typography variant="h3">{t('heading')}</Typography>
            <div className={classes.actionButtonContainer}>
              <RbacValidator
                customValidateFunction={() => {
                  return (
                    hasNecessaryPerm({
                      ...ApiPermissionMap.DR_CONFIG_SWITCHOVER,
                      onResource: xClusterConfig.sourceUniverseUUID
                    }) &&
                    hasNecessaryPerm({
                      ...ApiPermissionMap.DR_CONFIG_SWITCHOVER,
                      onResource: xClusterConfig.targetUniverseUUID
                    })
                  );
                }}
                isControl
              >
                <YBButton
                  variant="primary"
                  size="large"
                  type="button"
                  onClick={openSwitchoverModal}
                  disabled={!enabledDrConfigActions.includes(DrConfigAction.SWITCHOVER)}
                  data-testid={`${PANEL_ID}-switchover`}
                >
                  {t('actionButton.switchover')}
                </YBButton>
              </RbacValidator>
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
                      <RbacValidator
                        customValidateFunction={() => {
                          return (
                            hasNecessaryPerm({
                              ...ApiPermissionMap.DR_CONFIG_SET_TABLES,
                              onResource: xClusterConfig.sourceUniverseUUID
                            }) &&
                            hasNecessaryPerm({
                              ...ApiPermissionMap.DR_CONFIG_SET_TABLES,
                              onResource: xClusterConfig.targetUniverseUUID
                            })
                          );
                        }}
                        overrideStyle={{ display: 'block' }}
                        isControl
                      >
                        <MenuItem
                          eventKey={XClusterConfigAction.MANAGE_TABLE}
                          onSelect={openEditTablesModal}
                          disabled={
                            !enabledXClusterConfigActions.includes(
                              XClusterConfigAction.MANAGE_TABLE
                            )
                          }
                        >
                          <YBMenuItemLabel
                            label={t('actionButton.actionMenu.editTables')}
                            preLabelElement={<i className="fa fa-table" />}
                          />
                        </MenuItem>
                      </RbacValidator>
                      <RbacValidator
                        customValidateFunction={() => {
                          return (
                            hasNecessaryPerm({
                              ...ApiPermissionMap.DR_CONFIG_EDIT,
                              onResource: xClusterConfig.sourceUniverseUUID
                            }) &&
                            hasNecessaryPerm({
                              ...ApiPermissionMap.DR_CONFIG_EDIT,
                              onResource: xClusterConfig.targetUniverseUUID
                            })
                          );
                        }}
                        overrideStyle={{ display: 'block' }}
                        isControl
                      >
                        <MenuItem
                          eventKey={DrConfigAction.EDIT}
                          onSelect={openEditConfigModal}
                          disabled={!enabledDrConfigActions.includes(DrConfigAction.EDIT)}
                        >
                          <YBMenuItemLabel
                            label={t('actionButton.actionMenu.editDrConfig')}
                            preLabelElement={<i className="fa fa-gear" />}
                          />
                        </MenuItem>
                      </RbacValidator>
                      <RbacValidator
                        customValidateFunction={() => {
                          return (
                            hasNecessaryPerm({
                              ...ApiPermissionMap.DR_CONFIG_REPLACE_REPLICA,
                              onResource: xClusterConfig.sourceUniverseUUID
                            }) &&
                            hasNecessaryPerm({
                              ...ApiPermissionMap.DR_CONFIG_REPLACE_REPLICA,
                              onResource: xClusterConfig.targetUniverseUUID
                            })
                          );
                        }}
                        overrideStyle={{ display: 'block' }}
                        isControl
                      >
                        <MenuItem
                          eventKey={DrConfigAction.EDIT_TARGET}
                          onSelect={openEditTargetConfigModal}
                          disabled={!enabledDrConfigActions.includes(DrConfigAction.EDIT_TARGET)}
                        >
                          <YBMenuItemLabel
                            label={t('actionButton.actionMenu.editDrConfigTarget')}
                            preLabelElement={<i className="fa fa-globe" />}
                          />
                        </MenuItem>
                      </RbacValidator>
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
                      <RbacValidator
                        customValidateFunction={() => {
                          return (
                            hasNecessaryPerm({
                              ...ApiPermissionMap.DR_CONFIG_SWITCHOVER,
                              onResource: xClusterConfig.sourceUniverseUUID
                            }) &&
                            hasNecessaryPerm({
                              ...ApiPermissionMap.DR_CONFIG_SWITCHOVER,
                              onResource: xClusterConfig.targetUniverseUUID
                            })
                          );
                        }}
                        overrideStyle={{ display: 'block' }}
                        isControl
                      >
                        <MenuItem
                          eventKey={DrConfigAction.SWITCHOVER}
                          onSelect={openSwitchoverModal}
                          disabled={!enabledDrConfigActions.includes(DrConfigAction.SWITCHOVER)}
                        >
                          <YBMenuItemLabel
                            label={t('actionButton.actionMenu.switchover')}
                            preLabelElement={
                              <SwitchoverIcon
                                isDisabled={
                                  !enabledDrConfigActions.includes(DrConfigAction.SWITCHOVER)
                                }
                              />
                            }
                          />
                        </MenuItem>
                      </RbacValidator>
                      <RbacValidator
                        customValidateFunction={() => {
                          return (
                            hasNecessaryPerm({
                              ...ApiPermissionMap.DR_CONFIG_FAILOVER,
                              onResource: xClusterConfig.sourceUniverseUUID
                            }) &&
                            hasNecessaryPerm({
                              ...ApiPermissionMap.DR_CONFIG_FAILOVER,
                              onResource: xClusterConfig.targetUniverseUUID
                            })
                          );
                        }}
                        overrideStyle={{ display: 'block' }}
                        isControl
                      >
                        <MenuItem
                          eventKey={DrConfigAction.FAILOVER}
                          onSelect={openFailoverModal}
                          disabled={!enabledDrConfigActions.includes(DrConfigAction.FAILOVER)}
                        >
                          <YBMenuItemLabel
                            label={t('actionButton.actionMenu.failover')}
                            preLabelElement={
                              <FailoverIcon
                                isDisabled={
                                  !enabledDrConfigActions.includes(DrConfigAction.FAILOVER)
                                }
                              />
                            }
                          />
                        </MenuItem>
                      </RbacValidator>
                      <MenuItem divider />
                      <RbacValidator
                        customValidateFunction={() => {
                          return (
                            hasNecessaryPerm({
                              ...ApiPermissionMap.DELETE_DR_CONFIG,
                              onResource: xClusterConfig.sourceUniverseUUID
                            }) &&
                            hasNecessaryPerm({
                              ...ApiPermissionMap.DELETE_DR_CONFIG,
                              onResource: xClusterConfig.targetUniverseUUID
                            })
                          );
                        }}
                        overrideStyle={{ display: 'block' }}
                        isControl
                      >
                        <MenuItem
                          eventKey={DrConfigAction.DELETE}
                          onSelect={openDeleteConfigModal}
                          disabled={!enabledDrConfigActions.includes(DrConfigAction.DELETE)}
                        >
                          <YBMenuItemLabel
                            label={t('actionButton.actionMenu.deleteConfig')}
                            preLabelElement={<i className="fa fa-trash" />}
                          />
                        </MenuItem>
                      </RbacValidator>
                    </>
                  )}
                  subMenus={{
                    // eslint-disable-next-line react/display-name
                    [ActionMenu.ADVANCED]: (setActiveSubmenu) => (
                      <>
                        <MenuItem eventKey="back" onSelect={() => setActiveSubmenu(null)}>
                          <YBMenuItemLabel
                            label={t('back', { keyPrefix: 'common' })}
                            preLabelElement={<i className="fa fa-chevron-left fa-fw" />}
                          />
                        </MenuItem>
                        <RbacValidator
                          customValidateFunction={() => {
                            return (
                              hasNecessaryPerm({
                                ...ApiPermissionMap.DR_CONFIG_RESTART,
                                onResource: xClusterConfig.sourceUniverseUUID
                              }) &&
                              hasNecessaryPerm({
                                ...ApiPermissionMap.DR_CONFIG_RESTART,
                                onResource: xClusterConfig.targetUniverseUUID
                              })
                            );
                          }}
                          overrideStyle={{ display: 'block' }}
                          isControl
                        >
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
                        </RbacValidator>
                        <RbacValidator
                          customValidateFunction={() => {
                            return (
                              hasNecessaryPerm({
                                ...ApiPermissionMap.DR_CONFIG_SYNC,
                                onResource: xClusterConfig.sourceUniverseUUID
                              }) &&
                              hasNecessaryPerm({
                                ...ApiPermissionMap.DR_CONFIG_SYNC,
                                onResource: xClusterConfig.targetUniverseUUID
                              })
                            );
                          }}
                          overrideStyle={{ display: 'block' }}
                          isControl
                        >
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
                        </RbacValidator>
                      </>
                    )
                  }}
                />
              </DropdownButton>
            </div>
          </div>
          <Box display="flex" flexDirection="column" gridGap={theme.spacing(3)}>
            <DrConfigOverview drConfig={drConfig} />
            <DrConfigDetails
              drConfig={drConfig}
              isTableInfoIncludedInConfig={drConfigFullQuery.isSuccess}
            />
          </Box>
          {isSwitchoverModalOpen && (
            <InitiateSwitchoverModal
              drConfig={drConfig}
              allowedTasks={allowedTasks}
              modalProps={{ open: isSwitchoverModalOpen, onClose: closeSwitchoverModal }}
            />
          )}
          {isFailoverModalOpen && (
            <InitiateFailoverModal
              drConfig={drConfig}
              allowedTasks={allowedTasks}
              modalProps={{ open: isFailoverModalOpen, onClose: closeFailoverModal }}
            />
          )}
          {isDeleteConfigModalOpen && (
            <DeleteConfigModal
              drConfig={drConfig}
              allowedTasks={allowedTasks}
              currentUniverseName={currentUniverseQuery.data.name}
              redirectUrl={deleteConfigRedirectUrl}
              modalProps={{ open: isDeleteConfigModalOpen, onClose: closeDeleteConfigModal }}
            />
          )}
          {isEditConfigModalOpen && (
            <EditConfigModal
              drConfig={drConfig}
              allowedTasks={allowedTasks}
              modalProps={{ open: isEditConfigModalOpen, onClose: closeEditConfigModal }}
            />
          )}
          {isEditConfigTargetModalOpen && (
            <EditConfigTargetModal
              drConfig={drConfig}
              modalProps={{
                open: isEditConfigTargetModalOpen,
                onClose: closeEditTargetConfigModal
              }}
            />
          )}
          {isEditTablesModalOpen && (
            <EditTablesModal
              xClusterConfigUuid={xClusterConfig.uuid}
              isDrInterface={true}
              drConfigUuid={drConfig.uuid}
              storageConfigUuid={drConfig.bootstrapParams?.backupRequestParams?.storageConfigUUID}
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
              isDrInterface={true}
              allowedTasks={allowedTasks}
              drConfig={drConfig}
              isVisible={isRestartConfigModalOpen}
              onHide={closeRestartConfigModal}
              xClusterConfigUuid={xClusterConfig.uuid}
            />
          )}
          {isDbSyncModalOpen && (
            <SyncXClusterConfigModal
              allowedTasks={allowedTasks}
              xClusterConfig={xClusterConfig}
              isDrInterface={true}
              drConfigUuid={drConfig.uuid}
              modalProps={{ open: isDbSyncModalOpen, onClose: closeDbSyncModal }}
            />
          )}
        </Box>
      </RbacValidator>
    </>
  );
};

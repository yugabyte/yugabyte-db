import { useState } from 'react';
import { ButtonGroup, Col, DropdownButton, MenuItem, Row, Tab } from 'react-bootstrap';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { useDispatch, useSelector } from 'react-redux';
import { Link } from 'react-router';
import { toast } from 'react-toastify';
import { useInterval } from 'react-use';
import _ from 'lodash';
import { Box, Typography, useTheme } from '@material-ui/core';
import moment from 'moment';

import { closeDialog, openDialog } from '../../../actions/modal';
import {
  fetchXClusterConfig,
  fetchTaskUntilItCompletes,
  editXClusterState
} from '../../../actions/xClusterReplication';
import { YBButton } from '../../common/forms/fields';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { YBTabsPanel } from '../../panels';
import {
  XClusterConfigAction,
  TRANSITORY_XCLUSTER_CONFIG_STATUSES,
  XClusterConfigState,
  XClusterModalName,
  XClusterTableStatus,
  XCLUSTER_CONFIG_REFETCH_INTERVAL_MS,
  XCLUSTER_METRIC_REFETCH_INTERVAL_MS,
  MetricName,
  liveMetricTimeRangeUnit,
  liveMetricTimeRangeValue
} from '../constants';
import {
  MaxAcceptableLag,
  CurrentReplicationLag,
  getEnabledConfigActions,
  getStrictestReplicationLagAlertThreshold
} from '../ReplicationUtils';
import { LagGraph } from './LagGraph';
import { ReplicationTables } from './ReplicationTables';
import { ReplicationOverview } from './ReplicationOverview';
import { XClusterConfigStatusLabel } from '../XClusterConfigStatusLabel';
import { DeleteConfigModal } from './DeleteConfigModal';
import { RestartConfigModal } from '../restartConfig/RestartConfigModal';
import { YBBanner, YBBannerVariant, YBLabelWithIcon } from '../../common/descriptors';
import {
  alertConfigQueryKey,
  api,
  metricQueryKey,
  universeQueryKey,
  xClusterQueryKey
} from '../../../redesign/helpers/api';
import { getAlertConfigurations } from '../../../actions/universe';
import { MenuItemsContainer } from '../../universes/UniverseDetail/compounds/MenuItemsContainer';
import { SyncXClusterConfigModal } from './SyncXClusterModal';
import {
  RbacValidator,
  hasNecessaryPerm
} from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { EditTablesModal } from '../disasterRecovery/editTables/EditTablesModal';
import { XClusterMetrics } from '../sharedComponents/XClusterMetrics/XClusterMetrics';

import { XClusterConfig } from '../dtos';
import { MetricsQueryParams } from '../../../redesign/helpers/dtos';
import { NodeAggregation, SplitType } from '../../metrics/dtos';
import { AlertTemplate } from '../../../redesign/features/alerts/TemplateComposer/ICustomVariables';

import './ReplicationDetails.scss';

interface Props {
  params: {
    uuid: string;
    replicationUUID: string;
  };
}

const ActionMenu = {
  ADVANCED: 'advanced'
} as const;

export function ReplicationDetails({
  params: { uuid: currentUniverseUuid, replicationUUID: xClusterConfigUuid }
}: Props) {
  const [isActionDropdownOpen, setIsActionDropdownOpen] = useState(false);
  const { showModal, visibleModal } = useSelector((state: any) => state.modal);
  const dispatch = useDispatch();
  const queryClient = useQueryClient();
  const theme = useTheme();

  const xClusterConfigQuery = useQuery(xClusterQueryKey.detail(xClusterConfigUuid), () =>
    fetchXClusterConfig(xClusterConfigUuid)
  );
  const sourceUniverseQuery = useQuery(
    universeQueryKey.detail(xClusterConfigQuery.data?.sourceUniverseUUID),
    () => api.fetchUniverse(xClusterConfigQuery.data?.sourceUniverseUUID),
    { enabled: xClusterConfigQuery.data?.sourceUniverseUUID !== undefined }
  );

  const targetUniverseQuery = useQuery(
    universeQueryKey.detail(xClusterConfigQuery.data?.targetUniverseUUID),
    () => api.fetchUniverse(xClusterConfigQuery.data?.targetUniverseUUID),
    { enabled: xClusterConfigQuery.data?.targetUniverseUUID !== undefined }
  );

  const replicationLagMetricSettings = {
    metric: MetricName.ASYNC_REPLICATION_SENT_LAG,
    nodeAggregation: NodeAggregation.MAX,
    splitType: SplitType.TABLE
  };
  const replicationLagMetricRequestParams: MetricsQueryParams = {
    metricsWithSettings: [replicationLagMetricSettings],
    nodePrefix: sourceUniverseQuery.data?.universeDetails.nodePrefix,
    xClusterConfigUuid: xClusterConfigUuid,
    start: moment().subtract(liveMetricTimeRangeValue, liveMetricTimeRangeUnit).format('X'),
    end: moment().format('X')
  };
  const tableReplicationLagQuery = useQuery(
    metricQueryKey.live(
      replicationLagMetricRequestParams,
      liveMetricTimeRangeValue,
      liveMetricTimeRangeUnit
    ),
    () => api.fetchMetrics(replicationLagMetricRequestParams),
    {
      enabled: !!sourceUniverseQuery.data
    }
  );

  const alertConfigFilter = {
    template: AlertTemplate.REPLICATION_LAG,
    targetUuid: currentUniverseUuid
  };
  const replicationLagAlertConfigQuery = useQuery(alertConfigQueryKey.list(alertConfigFilter), () =>
    getAlertConfigurations(alertConfigFilter)
  );

  const toggleConfigPausedState = useMutation(
    (xClusterConfig: XClusterConfig) => {
      return editXClusterState(
        xClusterConfig.uuid,
        xClusterConfig.paused ? XClusterConfigState.RUNNING : XClusterConfigState.PAUSED
      );
    },
    {
      onSuccess: (resp, xClusterConfig) => {
        fetchTaskUntilItCompletes(resp.data.taskUUID, (err: boolean) => {
          if (!err) {
            queryClient.invalidateQueries(xClusterQueryKey.detail(xClusterConfig.uuid));
          } else {
            toast.error(
              <span className="alertMsg">
                <i className="fa fa-exclamation-circle" />
                <span>Task Failed.</span>
                <a href={`/tasks/${resp.data.taskUUID}`} target="_blank" rel="noopener noreferrer">
                  View Details
                </a>
              </span>
            );
          }
        });
      }
    }
  );

  useInterval(() => {
    queryClient.invalidateQueries('xcluster-metric');
    queryClient.invalidateQueries(metricQueryKey.live());
    if (
      xClusterConfigQuery.data !== undefined &&
      _.includes(TRANSITORY_XCLUSTER_CONFIG_STATUSES, xClusterConfigQuery.data.status)
    ) {
      queryClient.invalidateQueries(xClusterQueryKey.detail(xClusterConfigUuid));
    }
  }, XCLUSTER_METRIC_REFETCH_INTERVAL_MS);

  useInterval(() => {
    queryClient.invalidateQueries(xClusterQueryKey.detail(xClusterConfigUuid));
  }, XCLUSTER_CONFIG_REFETCH_INTERVAL_MS);

  if (xClusterConfigQuery.isLoading || xClusterConfigQuery.isIdle) {
    return <YBLoading />;
  }

  if (xClusterConfigQuery.isError) {
    const errorMessage =
      xClusterConfigQuery.error instanceof Error
        ? `Error fetching xCluster configuration: ${xClusterConfigQuery.error.message}`
        : 'Error fetching xCluster configuration';

    const customErrorMessage = (
      <>
        <div>{errorMessage}</div>
        <div>
          Click <Link to={`/universes/${currentUniverseUuid}/replication`}>here</Link> to go back to
          the xCluster configurations page.
        </div>
      </>
    );

    return <YBErrorIndicator customErrorMessage={customErrorMessage} />;
  }

  const allowedTasks = sourceUniverseQuery.data?.allowedTasks;
  const hideModal = () => dispatch(closeDialog());
  const isDeleteConfigModalVisible = showModal && visibleModal === XClusterModalName.DELETE_CONFIG;
  const xClusterConfig = xClusterConfigQuery.data;
  if (
    xClusterConfig.sourceUniverseUUID === undefined ||
    xClusterConfig.targetUniverseUUID === undefined
  ) {
    const errorMessage = `The ${
      xClusterConfig.sourceUniverseUUID === undefined ? 'source' : 'target'
    } universe is deleted. Please delete the broken xCluster configuration: ${xClusterConfig.name}`;
    const remainingUniverseUUID =
      xClusterConfig.sourceUniverseUUID ?? xClusterConfig.targetUniverseUUID;
    const redirectUrl = remainingUniverseUUID
      ? `/universes/${remainingUniverseUUID}/replication`
      : '/universes';

    return (
      <div className="xCluster-details-error-container">
        <YBErrorIndicator customErrorMessage={errorMessage} />
        <YBButton
          btnText="Delete Replication"
          btnClass="btn btn-orange delete-config-button"
          onClick={() => dispatch(openDialog(XClusterModalName.DELETE_CONFIG))}
        />
        {isDeleteConfigModalVisible && (
          <DeleteConfigModal
            allowedTasks={allowedTasks!}
            sourceUniverseUUID={xClusterConfig.sourceUniverseUUID}
            targetUniverseUUID={xClusterConfig.targetUniverseUUID}
            xClusterConfigUUID={xClusterConfig.uuid}
            xClusterConfigName={xClusterConfig.name}
            onHide={hideModal}
            visible={showModal && visibleModal === XClusterModalName.DELETE_CONFIG}
            redirectUrl={redirectUrl}
          />
        )}
      </div>
    );
  }

  if (
    sourceUniverseQuery.isLoading ||
    sourceUniverseQuery.isIdle ||
    targetUniverseQuery.isLoading ||
    targetUniverseQuery.isIdle
  ) {
    return <YBLoading />;
  }

  if (sourceUniverseQuery.isError || targetUniverseQuery.isError) {
    return (
      <YBErrorIndicator customErrorMessage="Error fetching information for participating universes." />
    );
  }

  const sourceUniverse = sourceUniverseQuery.data;
  const targetUniverse = targetUniverseQuery.data;
  const enabledConfigActions = getEnabledConfigActions(
    xClusterConfig,
    sourceUniverse,
    targetUniverse
  );

  let numTablesAboveLagThreshold = 0;
  if (replicationLagAlertConfigQuery.isSuccess && tableReplicationLagQuery.isSuccess) {
    const maxAcceptableLag = getStrictestReplicationLagAlertThreshold(
      replicationLagAlertConfigQuery.data
    );
    tableReplicationLagQuery.data.async_replication_sent_lag?.data.forEach((trace) => {
      if (
        trace.y[trace.y.length - 1] &&
        maxAcceptableLag &&
        trace.y[trace.y.length - 1] > maxAcceptableLag
      ) {
        numTablesAboveLagThreshold += 1;
      }
    });
  }

  const numTablesRequiringBootstrap = xClusterConfig.tableDetails.reduce(
    (errorCount: number, xClusterTableDetails) => {
      return xClusterTableDetails.status === XClusterTableStatus.ERROR
        ? errorCount + 1
        : errorCount;
    },
    0
  );

  const xClusterConfigTables = xClusterConfigQuery.data?.tableDetails ?? [];
  const shouldShowConfigError = numTablesRequiringBootstrap > 0;
  const shouldShowTableLagWarning =
    replicationLagAlertConfigQuery.isSuccess &&
    tableReplicationLagQuery.isSuccess &&
    numTablesAboveLagThreshold > 0 &&
    xClusterConfigTables.length > 0;
  const isEditTableModalVisible = showModal && visibleModal === XClusterModalName.EDIT_TABLES;
  const isRestartConfigModalVisible =
    showModal && visibleModal === XClusterModalName.RESTART_CONFIG;
  const isSyncConfigModalVisible =
    showModal && visibleModal === XClusterModalName.SYNC_XCLUSTER_CONFIG_WITH_DB;
  return (
    <>
      <div className="replication-details">
        <h2 className="content-title">
          <Link to={`/universes/${xClusterConfig.sourceUniverseUUID}`}>{sourceUniverse.name}</Link>
          <span className="subtext">
            <i className="fa fa-chevron-right submenu-icon" />
            <Link to={`/universes/${xClusterConfig.sourceUniverseUUID}/replication/`}>
              xCluster Replication
            </Link>
            <i className="fa fa-chevron-right submenu-icon" />
            {xClusterConfig.name}
          </span>
        </h2>
        <div className="details-canvas">
          <Row className="replication-actions">
            <Col lg={7}>
              <h3>{xClusterConfig.name}</h3>
            </Col>
            <Col lg={5} className="noPadding">
              <Row className="details-actions-button">
                <RbacValidator
                  customValidateFunction={() => {
                    return (
                      hasNecessaryPerm({
                        ...ApiPermissionMap.MODIFY_XCLUSTER_REPLICATION,
                        onResource: xClusterConfig.sourceUniverseUUID
                      }) &&
                      hasNecessaryPerm({
                        ...ApiPermissionMap.MODIFY_XCLUSTER_REPLICATION,
                        onResource: xClusterConfig.targetUniverseUUID
                      })
                    );
                  }}
                  isControl
                >
                  <YBButton
                    btnText={`${xClusterConfig.paused ? 'Enable' : 'Pause'} Replication`}
                    btnClass="btn btn-orange replication-status-button"
                    disabled={
                      !_.includes(
                        enabledConfigActions,
                        xClusterConfig.paused
                          ? XClusterConfigAction.RESUME
                          : XClusterConfigAction.PAUSE
                      )
                    }
                    onClick={() => {
                      toast.success(
                        `${xClusterConfig.paused ? 'Enabling' : 'Pausing'} Replication...`
                      );
                      toggleConfigPausedState.mutateAsync(xClusterConfig);
                    }}
                  />
                </RbacValidator>
                <ButtonGroup className="more-actions-button">
                  <DropdownButton
                    pullRight
                    id="alert-mark-as-button"
                    title="Actions"
                    onToggle={(isOpen) => setIsActionDropdownOpen(isOpen)}
                  >
                    <MenuItemsContainer
                      parentDropdownOpen={isActionDropdownOpen}
                      mainMenu={(showSubmenu) => (
                        <>
                          <RbacValidator
                            customValidateFunction={() => {
                              return (
                                hasNecessaryPerm({
                                  ...ApiPermissionMap.MODIFY_XCLUSTER_REPLICATION,
                                  onResource: xClusterConfig.sourceUniverseUUID
                                }) &&
                                hasNecessaryPerm({
                                  ...ApiPermissionMap.MODIFY_XCLUSTER_REPLICATION,
                                  onResource: xClusterConfig.targetUniverseUUID
                                })
                              );
                            }}
                            isControl
                          >
                            <MenuItem
                              onSelect={() => dispatch(openDialog(XClusterModalName.EDIT_TABLES))}
                              disabled={
                                !_.includes(enabledConfigActions, XClusterConfigAction.MANAGE_TABLE)
                              }
                            >
                              <YBLabelWithIcon
                                className="xCluster-dropdown-button"
                                icon="fa fa-pencil"
                              >
                                Select Databases and Tables
                              </YBLabelWithIcon>
                            </MenuItem>
                          </RbacValidator>
                          <RbacValidator
                            customValidateFunction={() => {
                              return (
                                hasNecessaryPerm({
                                  ...ApiPermissionMap.MODIFY_XCLUSTER_REPLICATION,
                                  onResource: xClusterConfig.sourceUniverseUUID
                                }) &&
                                hasNecessaryPerm({
                                  ...ApiPermissionMap.MODIFY_XCLUSTER_REPLICATION,
                                  onResource: xClusterConfig.targetUniverseUUID
                                })
                              );
                            }}
                            isControl
                          >
                            <MenuItem
                              onClick={() => {
                                if (
                                  _.includes(enabledConfigActions, XClusterConfigAction.RESTART)
                                ) {
                                  dispatch(openDialog(XClusterModalName.RESTART_CONFIG));
                                }
                              }}
                              disabled={
                                !_.includes(enabledConfigActions, XClusterConfigAction.RESTART)
                              }
                            >
                              <YBLabelWithIcon
                                className="xCluster-dropdown-button"
                                icon="fa fa-refresh"
                              >
                                Restart Replication
                              </YBLabelWithIcon>
                            </MenuItem>
                          </RbacValidator>
                          <MenuItem onClick={() => showSubmenu(ActionMenu.ADVANCED)}>
                            <YBLabelWithIcon className="xCluster-dropdown-button" icon="fa fa-cogs">
                              Advanced
                              <span className="pull-right">
                                <i className="fa fa-chevron-right submenu-icon" />
                              </span>
                            </YBLabelWithIcon>
                          </MenuItem>
                          <MenuItem divider />
                          <RbacValidator
                            customValidateFunction={() => {
                              return (
                                hasNecessaryPerm({
                                  ...ApiPermissionMap.DELETE_XCLUSTER_REPLICATION,
                                  onResource: xClusterConfig.sourceUniverseUUID
                                }) &&
                                hasNecessaryPerm({
                                  ...ApiPermissionMap.DELETE_XCLUSTER_REPLICATION,
                                  onResource: xClusterConfig.targetUniverseUUID
                                })
                              );
                            }}
                            isControl
                          >
                            <MenuItem
                              onClick={() => {
                                if (_.includes(enabledConfigActions, XClusterConfigAction.DELETE)) {
                                  dispatch(openDialog(XClusterModalName.DELETE_CONFIG));
                                }
                              }}
                              disabled={
                                !_.includes(enabledConfigActions, XClusterConfigAction.DELETE)
                              }
                            >
                              <YBLabelWithIcon
                                className="xCluster-dropdown-button"
                                icon="fa fa-times"
                              >
                                Delete Replication
                              </YBLabelWithIcon>
                            </MenuItem>
                          </RbacValidator>
                        </>
                      )}
                      subMenus={{
                        // eslint-disable-next-line react/display-name
                        [ActionMenu.ADVANCED]: (navigateToMainMenu) => (
                          <>
                            <MenuItem onClick={navigateToMainMenu}>
                              <YBLabelWithIcon icon="fa fa-chevron-left fa-fw">
                                Back
                              </YBLabelWithIcon>
                            </MenuItem>
                            <RbacValidator
                              accessRequiredOn={{
                                ...ApiPermissionMap.SYNC_XCLUSTER,
                                onResource: xClusterConfig.targetUniverseUUID
                              }}
                              isControl
                            >
                              <MenuItem
                                onClick={() => {
                                  if (
                                    _.includes(enabledConfigActions, XClusterConfigAction.DB_SYNC)
                                  ) {
                                    dispatch(
                                      openDialog(XClusterModalName.SYNC_XCLUSTER_CONFIG_WITH_DB)
                                    );
                                  }
                                }}
                                disabled={
                                  !_.includes(enabledConfigActions, XClusterConfigAction.DB_SYNC)
                                }
                              >
                                <YBLabelWithIcon
                                  className="xCluster-dropdown-button"
                                  icon="fa fa-refresh"
                                >
                                  Reconcile config with DB
                                </YBLabelWithIcon>
                              </MenuItem>
                            </RbacValidator>
                          </>
                        )
                      }}
                    />
                  </DropdownButton>
                </ButtonGroup>
              </Row>
            </Col>
          </Row>
          <div className="replication-info-banners-container">
            {shouldShowConfigError && (
              <YBBanner variant={YBBannerVariant.DANGER}>
                <div className="replication-info-banner-content">
                  <b>Error!</b>
                  {` Write-ahead logs are deleted for ${numTablesRequiringBootstrap} ${
                    numTablesRequiringBootstrap > 1 ? 'tables' : 'table'
                  } and replication restart is
                required.`}
                  <RbacValidator
                    customValidateFunction={() => {
                      return (
                        hasNecessaryPerm({
                          ...ApiPermissionMap.MODIFY_XCLUSTER_REPLICATION,
                          onResource: xClusterConfig.sourceUniverseUUID
                        }) &&
                        hasNecessaryPerm({
                          ...ApiPermissionMap.MODIFY_XCLUSTER_REPLICATION,
                          onResource: xClusterConfig.targetUniverseUUID
                        })
                      );
                    }}
                    isControl
                  >
                    <YBButton
                      className="restart-replication-button"
                      btnIcon="fa fa-refresh"
                      btnText="Restart Replication"
                      onClick={() => {
                        if (_.includes(enabledConfigActions, XClusterConfigAction.RESTART)) {
                          dispatch(openDialog(XClusterModalName.RESTART_CONFIG));
                        }
                      }}
                      disabled={!_.includes(enabledConfigActions, XClusterConfigAction.RESTART)}
                    />
                  </RbacValidator>
                </div>
              </YBBanner>
            )}
            {shouldShowTableLagWarning && (
              <YBBanner variant={YBBannerVariant.WARNING}>
                <b>Warning!</b>
                {` Replication lag for ${numTablesAboveLagThreshold} out of ${
                  xClusterConfigTables.length
                } ${xClusterConfigTables.length > 1 ? 'tables' : 'table'} ${
                  numTablesAboveLagThreshold > 1 ? 'have' : 'has'
                }
                exceeded the maximum acceptable lag time.`}
              </YBBanner>
            )}
          </div>
          <Row className="replication-status">
            <Col lg={4}>
              <Box display="flex" alignItems="center" gridGap={theme.spacing(1)}>
                <Typography variant="body2">Replication Status</Typography>
                <XClusterConfigStatusLabel xClusterConfig={xClusterConfig} />
              </Box>
            </Col>
            <Col lg={8} className="lag-status-graph">
              <div className="lag-stats">
                <Box display="flex">
                  <Box whiteSpace="pre-wrap" width="50%">
                    Current Lag
                  </Box>
                  <Box marginLeft={2}>
                    <span className="lag-text">
                      <CurrentReplicationLag
                        xClusterConfigUuid={xClusterConfig.uuid}
                        xClusterConfigStatus={xClusterConfig.status}
                        sourceUniverseUuid={xClusterConfig.sourceUniverseUUID}
                      />
                    </span>
                  </Box>
                </Box>
                <div className="replication-divider" />
                <Box display="flex">
                  <Box whiteSpace="pre-wrap" width="50%">
                    Lowest Replication Lag Alert Threshold
                  </Box>
                  <Box marginLeft={2}>
                    <span className="lag-value">
                      <MaxAcceptableLag currentUniverseUUID={xClusterConfig.sourceUniverseUUID} />
                    </span>
                  </Box>
                </Box>
              </div>
              <div>
                <LagGraph
                  replicationUUID={xClusterConfig.uuid}
                  sourceUniverseUUID={xClusterConfig.sourceUniverseUUID}
                />
              </div>
            </Col>
          </Row>
          <Row className="replication-details-panel noPadding">
            <Col lg={12} className="noPadding">
              <YBTabsPanel defaultTab={'overview'} id="replication-tab-panel">
                <Tab eventKey={'overview'} title={'Overview'}>
                  {targetUniverse !== undefined && (
                    <ReplicationOverview
                      xClusterConfig={xClusterConfig}
                      destinationUniverse={targetUniverse}
                    />
                  )}
                </Tab>
                <Tab eventKey={'tables'} title={'Tables'}>
                  <ReplicationTables
                    xClusterConfig={xClusterConfig}
                    isActive={window.location.search === '?tab=tables'}
                    isDrInterface={false}
                  />
                </Tab>
                <Tab eventKey={'metrics'} title="Metrics" id="universe-tab-panel">
                  <XClusterMetrics xClusterConfig={xClusterConfig} isDrInterface={false} />
                </Tab>
              </YBTabsPanel>
            </Col>
          </Row>
        </div>
        {isEditTableModalVisible && (
          <EditTablesModal
            xClusterConfigUuid={xClusterConfig.uuid}
            isDrInterface={false}
            modalProps={{ open: isEditTableModalVisible, onClose: hideModal }}
          />
        )}
        {isDeleteConfigModalVisible && (
          <DeleteConfigModal
            allowedTasks={allowedTasks!}
            sourceUniverseUUID={xClusterConfig.sourceUniverseUUID}
            targetUniverseUUID={xClusterConfig.targetUniverseUUID}
            xClusterConfigUUID={xClusterConfig.uuid}
            xClusterConfigName={xClusterConfig.name}
            onHide={hideModal}
            visible={isDeleteConfigModalVisible}
            redirectUrl={`/universes/${currentUniverseUuid}/replication`}
          />
        )}
        {isRestartConfigModalVisible && (
          <RestartConfigModal
            isDrInterface={false}
            allowedTasks={allowedTasks!}
            isVisible={isRestartConfigModalVisible}
            onHide={hideModal}
            xClusterConfigUuid={xClusterConfig.uuid}
          />
        )}
        {isSyncConfigModalVisible && (
          <SyncXClusterConfigModal
            allowedTasks={allowedTasks!}
            xClusterConfig={xClusterConfig}
            isDrInterface={false}
            modalProps={{ open: isSyncConfigModalVisible, onClose: hideModal }}
          />
        )}
      </div>
    </>
  );
}

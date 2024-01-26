import { useState } from 'react';
import { ButtonGroup, Col, DropdownButton, MenuItem, Row, Tab } from 'react-bootstrap';
import { useMutation, useQueries, useQuery, useQueryClient, UseQueryResult } from 'react-query';
import { useDispatch, useSelector } from 'react-redux';
import { Link } from 'react-router';
import { toast } from 'react-toastify';
import { useInterval } from 'react-use';
import _ from 'lodash';
import { Box, Typography, useTheme } from '@material-ui/core';

import { closeDialog, openDialog } from '../../../actions/modal';
import {
  fetchXClusterConfig,
  fetchTaskUntilItCompletes,
  editXClusterState,
  queryLagMetricsForTable,
  fetchTablesInUniverse
} from '../../../actions/xClusterReplication';
import { YBButton } from '../../common/forms/fields';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { YBTabsPanel } from '../../panels';
import { ReplicationContainer } from '../../tables';
import {
  XClusterConfigAction,
  REPLICATION_LAG_ALERT_NAME,
  TRANSITORY_XCLUSTER_CONFIG_STATUSES,
  XClusterConfigState,
  XClusterModalName,
  XClusterTableStatus,
  XCLUSTER_CONFIG_REFETCH_INTERVAL_MS,
  XCLUSTER_METRIC_REFETCH_INTERVAL_MS
} from '../constants';
import {
  MaxAcceptableLag,
  CurrentReplicationLag,
  getEnabledConfigActions,
  getXClusterConfigTableType,
  getLatestMaxNodeLag
} from '../ReplicationUtils';
import { EditConfigModal } from './EditConfigModal';
import { LagGraph } from './LagGraph';
import { ReplicationTables } from './ReplicationTables';
import { ReplicationOverview } from './ReplicationOverview';
import { XClusterConfigStatusLabel } from '../XClusterConfigStatusLabel';
import { DeleteConfigModal } from './DeleteConfigModal';
import { RestartConfigModal } from '../restartConfig/RestartConfigModal';
import { YBBanner, YBBannerVariant, YBLabelWithIcon } from '../../common/descriptors';
import { api, universeQueryKey, xClusterQueryKey } from '../../../redesign/helpers/api';
import { getAlertConfigurations } from '../../../actions/universe';
import { MenuItemsContainer } from '../../universes/UniverseDetail/compounds/MenuItemsContainer';
import { SyncXClusterConfigModal } from './SyncXClusterModal';

import { Metrics } from '../XClusterTypes';
import { XClusterConfig } from '../dtos';
import { TableType, YBTable } from '../../../redesign/helpers/dtos';

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
  params: { uuid: currentUniverseUUID, replicationUUID: xClusterConfigUUID }
}: Props) {
  const [isActionDropdownOpen, setIsActionDropdownOpen] = useState(false);
  const { showModal, visibleModal } = useSelector((state: any) => state.modal);
  const dispatch = useDispatch();
  const queryClient = useQueryClient();
  const theme = useTheme();

  const xClusterConfigQuery = useQuery(xClusterQueryKey.detail(xClusterConfigUUID), () =>
    fetchXClusterConfig(xClusterConfigUUID)
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

  const sourceUniverseTableQuery = useQuery<YBTable[]>(
    universeQueryKey.tables(xClusterConfigQuery.data?.sourceUniverseUUID, {
      excludeColocatedTables: true
    }),
    () =>
      fetchTablesInUniverse(xClusterConfigQuery.data?.sourceUniverseUUID, {
        excludeColocatedTables: true
      }).then((response) => response.data),
    { enabled: xClusterConfigQuery.data?.sourceUniverseUUID !== undefined }
  );

  const xClusterConfigTables = xClusterConfigQuery.data?.tableDetails ?? [];
  const tableLagQueries = useQueries(
    xClusterConfigTables.map((xClusterTable) => ({
      queryKey: [
        'xcluster-metric',
        sourceUniverseQuery.data?.universeDetails.nodePrefix,
        xClusterTable.tableId,
        xClusterTable.streamId,
        'metric'
      ],
      queryFn: () =>
        queryLagMetricsForTable(
          xClusterTable.streamId,
          xClusterTable.tableId,
          sourceUniverseQuery.data?.universeDetails.nodePrefix
        ),
      enabled: !!sourceUniverseQuery.data
    }))
  ) as UseQueryResult<Metrics<'tserver_async_replication_lag_micros'>>[];

  const alertConfigFilter = {
    name: REPLICATION_LAG_ALERT_NAME,
    targetUuid: currentUniverseUUID
  };
  const maxAcceptableLagQuery = useQuery(['alert', 'configurations', alertConfigFilter], () =>
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
    if (
      xClusterConfigQuery.data !== undefined &&
      _.includes(TRANSITORY_XCLUSTER_CONFIG_STATUSES, xClusterConfigQuery.data.status)
    ) {
      queryClient.invalidateQueries(xClusterQueryKey.detail(xClusterConfigUUID));
    }
  }, XCLUSTER_METRIC_REFETCH_INTERVAL_MS);

  useInterval(() => {
    queryClient.invalidateQueries(xClusterQueryKey.detail(xClusterConfigUUID));
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
          Click <Link to={`/universes/${currentUniverseUUID}/replication`}>here</Link> to go back to
          the xCluster configurations page.
        </div>
      </>
    );

    return <YBErrorIndicator customErrorMessage={customErrorMessage} />;
  }

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
    targetUniverseQuery.isIdle ||
    sourceUniverseTableQuery.isLoading ||
    sourceUniverseTableQuery.isIdle
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
  const configTableType = getXClusterConfigTableType(xClusterConfig);
  if (configTableType === undefined || sourceUniverseTableQuery.isError) {
    const redirectUrl = `/universes/${xClusterConfig.sourceUniverseUUID}/replication`;
    const errorMessage = sourceUniverseTableQuery.isError
      ? 'Encounter an error fetching information for tables from the source universe.'
      : 'Unexpected state. All tables in the xCluster config were dropped in the source universe.';
    return (
      <div className="xCluster-details-error-container">
        <YBErrorIndicator customErrorMessage={errorMessage} />
        <Box display="flex" flexDirection="column" gridGap={theme.spacing(1)}>
          <Typography variant="h4">{`Replication Name: ${xClusterConfig.name}`}</Typography>
          <Typography variant="h4">
            {'Replication Status: '}
            <XClusterConfigStatusLabel xClusterConfig={xClusterConfig} />
          </Typography>
        </Box>
        <Box display="flex" marginTop={3} gridGap={theme.spacing(1)}>
          {!xClusterConfig.paused && (
            <YBButton
              btnText="Pause Replication"
              btnClass="btn btn-orange"
              disabled={!_.includes(enabledConfigActions, XClusterConfigAction.PAUSE)}
              onClick={() => {
                toast.success('Pausing Replication...');
                toggleConfigPausedState.mutateAsync(xClusterConfig);
              }}
            />
          )}
          <YBButton
            btnText="Delete Replication"
            btnClass="btn btn-orange"
            onClick={() => dispatch(openDialog(XClusterModalName.DELETE_CONFIG))}
          />
          {isDeleteConfigModalVisible && (
            <DeleteConfigModal
              sourceUniverseUUID={xClusterConfig.sourceUniverseUUID}
              targetUniverseUUID={xClusterConfig.targetUniverseUUID}
              xClusterConfigUUID={xClusterConfig.uuid}
              xClusterConfigName={xClusterConfig.name}
              onHide={hideModal}
              visible={showModal && visibleModal === XClusterModalName.DELETE_CONFIG}
              redirectUrl={redirectUrl}
            />
          )}
        </Box>
      </div>
    );
  }

  if (
    configTableType !== TableType.PGSQL_TABLE_TYPE &&
    configTableType !== TableType.YQL_TABLE_TYPE
  ) {
    // Unexpected state. Illegal xCluster table type.
    return (
      <YBErrorIndicator
        customErrorMessage={`Unexpected state. Illegal xCluster table type: ${configTableType}`}
      />
    );
  }

  let numTablesAboveLagThreshold = 0;
  if (maxAcceptableLagQuery.isSuccess) {
    // TODO: Add type for alert configurations.
    const maxAcceptableLag = Math.min(
      ...maxAcceptableLagQuery.data.map(
        (alertConfig: any): number => alertConfig.thresholds.SEVERE.threshold
      )
    );
    for (const tableLagQuery of tableLagQueries) {
      if (tableLagQuery.isSuccess) {
        const maxNodeLag = getLatestMaxNodeLag(tableLagQuery.data);
        if (maxNodeLag && maxNodeLag > maxAcceptableLag) {
          numTablesAboveLagThreshold += 1;
        }
      }
    }
  }

  const numTablesRequiringBootstrap = xClusterConfig.tableDetails.reduce(
    (errorCount: number, xClusterTableDetails) => {
      return xClusterTableDetails.status === XClusterTableStatus.ERROR
        ? errorCount + 1
        : errorCount;
    },
    0
  );

  const shouldShowConfigError = numTablesRequiringBootstrap > 0;
  const shouldShowTableLagWarning =
    maxAcceptableLagQuery.isSuccess &&
    numTablesAboveLagThreshold > 0 &&
    xClusterConfigTables.length > 0;
  const isEditConfigModalVisible = showModal && visibleModal === XClusterModalName.EDIT_CONFIG;
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
              Replication
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
                          <MenuItem
                            onClick={() => {
                              if (_.includes(enabledConfigActions, XClusterConfigAction.EDIT)) {
                                dispatch(openDialog(XClusterModalName.EDIT_CONFIG));
                              }
                            }}
                            disabled={!_.includes(enabledConfigActions, XClusterConfigAction.EDIT)}
                          >
                            <YBLabelWithIcon
                              className="xCluster-dropdown-button"
                              icon="fa fa-pencil"
                            >
                              Edit Replication Name
                            </YBLabelWithIcon>
                          </MenuItem>
                          <MenuItem
                            onClick={() => {
                              if (_.includes(enabledConfigActions, XClusterConfigAction.RESTART)) {
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
                          <MenuItem onClick={() => showSubmenu(ActionMenu.ADVANCED)}>
                            <YBLabelWithIcon className="xCluster-dropdown-button" icon="fa fa-cogs">
                              Advanced
                              <span className="pull-right">
                                <i className="fa fa-chevron-right submenu-icon" />
                              </span>
                            </YBLabelWithIcon>
                          </MenuItem>
                          <MenuItem divider />
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
                <Row>
                  <Col lg={6}>Current Lag</Col>
                  <Col lg={6}>
                    <span className="lag-text">
                      <CurrentReplicationLag
                        xClusterConfigUUID={xClusterConfig.uuid}
                        xClusterConfigStatus={xClusterConfig.status}
                        sourceUniverseUUID={xClusterConfig.sourceUniverseUUID}
                      />
                    </span>
                  </Col>
                </Row>
                <div className="replication-divider" />
                <Row>
                  <Col lg={6}>Max acceptable lag</Col>
                  <Col lg={6}>
                    <span className="lag-value">
                      <MaxAcceptableLag currentUniverseUUID={xClusterConfig.sourceUniverseUUID} />
                    </span>
                  </Col>
                </Row>
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
                  <ReplicationTables xClusterConfig={xClusterConfig} />
                </Tab>
                <Tab eventKey={'metrics'} title="Metrics" id="universe-tab-panel">
                  <ReplicationContainer
                    sourceUniverseUUID={xClusterConfig.sourceUniverseUUID}
                    hideHeader={true}
                    replicationUUID={xClusterConfigUUID}
                  />
                </Tab>
              </YBTabsPanel>
            </Col>
          </Row>
        </div>
        {isEditConfigModalVisible && (
          <EditConfigModal
            xClusterConfig={xClusterConfig}
            visible={isEditConfigModalVisible}
            onHide={hideModal}
          />
        )}
        {isDeleteConfigModalVisible && (
          <DeleteConfigModal
            sourceUniverseUUID={xClusterConfig.sourceUniverseUUID}
            targetUniverseUUID={xClusterConfig.targetUniverseUUID}
            xClusterConfigUUID={xClusterConfig.uuid}
            xClusterConfigName={xClusterConfig.name}
            onHide={hideModal}
            visible={isDeleteConfigModalVisible}
            redirectUrl={`/universes/${currentUniverseUUID}/replication`}
          />
        )}
        {isRestartConfigModalVisible && (
          <RestartConfigModal
            configTableType={configTableType}
            isVisible={isRestartConfigModalVisible}
            onHide={hideModal}
            xClusterConfig={xClusterConfig}
          />
        )}
        {isSyncConfigModalVisible && (
          <SyncXClusterConfigModal
            xClusterConfig={xClusterConfig}
            modalProps={{ open: isSyncConfigModalVisible, onClose: hideModal }}
          />
        )}
      </div>
    </>
  );
}

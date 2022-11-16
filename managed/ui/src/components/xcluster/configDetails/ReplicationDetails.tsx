import React from 'react';
import { ButtonGroup, Col, DropdownButton, MenuItem, Row, Tab } from 'react-bootstrap';
import { useMutation, useQueries, useQuery, useQueryClient, UseQueryResult } from 'react-query';
import { useDispatch, useSelector } from 'react-redux';
import { Link } from 'react-router';
import { toast } from 'react-toastify';
import { useInterval } from 'react-use';
import _ from 'lodash';

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
  MetricName,
  MetricTraceName,
  XClusterConfigAction,
  REPLICATION_LAG_ALERT_NAME,
  TRANSITORY_STATES,
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
  parseFloatIfDefined,
  getXClusterConfigTableType
} from '../ReplicationUtils';
import { AddTableModal } from './addTable/AddTableModal';
import { EditConfigModal } from './EditConfigModal';
import { LagGraph } from './LagGraph';
import { ReplicationTables } from './ReplicationTables';
import { ReplicationOverview } from './ReplicationOverview';
import { XClusterConfigStatusLabel } from '../XClusterConfigStatusLabel';
import { DeleteConfigModal } from './DeleteConfigModal';
import { RestartConfigModal } from '../restartConfig/RestartConfigModal';
import { YBBanner, YBBannerVariant, YBLabelWithIcon } from '../../common/descriptors';
import { api } from '../../../redesign/helpers/api';
import { getAlertConfigurations } from '../../../actions/universe';

import { Metrics, XClusterConfig } from '../XClusterTypes';
import { TableType, YBTable } from '../../../redesign/helpers/dtos';

import './ReplicationDetails.scss';

interface Props {
  params: {
    uuid: string;
    replicationUUID: string;
  };
}

const COMMITTED_LAG_METRIC_TRACE_NAME =
  MetricTraceName[MetricName.TSERVER_ASYNC_REPLICATION_LAG_METRIC].COMMITTED_LAG;

export function ReplicationDetails({
  params: { uuid: currentUniverseUUID, replicationUUID: xClusterConfigUUID }
}: Props) {
  const { showModal, visibleModal } = useSelector((state: any) => state.modal);
  const dispatch = useDispatch();
  const queryClient = useQueryClient();

  const xClusterConfigQuery = useQuery(['Xcluster', xClusterConfigUUID], () =>
    fetchXClusterConfig(xClusterConfigUUID)
  );
  const sourceUniverseQuery = useQuery(
    ['universe', xClusterConfigQuery.data?.sourceUniverseUUID],
    () => api.fetchUniverse(xClusterConfigQuery.data?.sourceUniverseUUID),
    { enabled: xClusterConfigQuery.data?.sourceUniverseUUID !== undefined }
  );

  const targetUniverseQuery = useQuery(
    ['universe', xClusterConfigQuery.data?.targetUniverseUUID],
    () => api.fetchUniverse(xClusterConfigQuery.data?.targetUniverseUUID),
    { enabled: xClusterConfigQuery.data?.targetUniverseUUID !== undefined }
  );

  const sourceUniverseTableQuery = useQuery<YBTable[]>(
    ['universe', xClusterConfigQuery.data?.sourceUniverseUUID, 'tables'],
    () =>
      fetchTablesInUniverse(xClusterConfigQuery.data?.sourceUniverseUUID).then(
        (respone) => respone.data
      ),
    { enabled: !!xClusterConfigQuery.data }
  );

  const xClusterConfigTableUUIDs = xClusterConfigQuery.data?.tables ?? [];
  const tableLagQueries = useQueries(
    xClusterConfigTableUUIDs.map((tableUUID) => ({
      queryKey: [
        'xcluster-metric',
        sourceUniverseQuery.data?.universeDetails.nodePrefix,
        tableUUID,
        'metric'
      ],
      queryFn: () =>
        queryLagMetricsForTable(tableUUID, sourceUniverseQuery.data?.universeDetails.nodePrefix),
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
    (replication: XClusterConfig) => {
      return editXClusterState(
        replication,
        replication.paused ? XClusterConfigState.RUNNING : XClusterConfigState.PAUSED
      );
    },
    {
      onSuccess: (resp, replication) => {
        fetchTaskUntilItCompletes(resp.data.taskUUID, (err: boolean) => {
          if (!err) {
            queryClient.invalidateQueries(['Xcluster', replication.uuid]);
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
      _.includes(TRANSITORY_STATES, xClusterConfigQuery.data.status)
    ) {
      queryClient.invalidateQueries(['Xcluster', xClusterConfigQuery.data.uuid]);
    }
  }, XCLUSTER_METRIC_REFETCH_INTERVAL_MS);

  useInterval(() => {
    queryClient.invalidateQueries(['Xcluster', xClusterConfigQuery.data?.uuid]);
  }, XCLUSTER_CONFIG_REFETCH_INTERVAL_MS);

  if (
    xClusterConfigQuery.isLoading ||
    xClusterConfigQuery.isIdle ||
    sourceUniverseQuery.isLoading ||
    sourceUniverseQuery.isIdle ||
    targetUniverseQuery.isLoading ||
    targetUniverseQuery.isIdle ||
    sourceUniverseTableQuery.isLoading ||
    sourceUniverseTableQuery.isIdle
  ) {
    return <YBLoading />;
  }

  if (
    xClusterConfigQuery.isError ||
    sourceUniverseQuery.isError ||
    targetUniverseQuery.isError ||
    sourceUniverseTableQuery.isError
  ) {
    return <YBErrorIndicator />;
  }

  const xClusterConfig = xClusterConfigQuery.data;
  const configTableType = getXClusterConfigTableType(xClusterConfig, sourceUniverseTableQuery.data);
  if (configTableType === undefined) {
    return (
      <YBErrorIndicator customErrorMessage="Unexpected state. A table in the xCluster config was not found amongst its source universe tables." />
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
        const metric = tableLagQuery.data.tserver_async_replication_lag_micros;
        const traceAlias = metric.layout.yaxis.alias[COMMITTED_LAG_METRIC_TRACE_NAME];
        const trace = metric.data.find((trace) => (trace.name = traceAlias));
        const latestLag = parseFloatIfDefined(trace?.y[trace.y.length - 1]);

        if (latestLag === undefined || latestLag > maxAcceptableLag) {
          numTablesAboveLagThreshold += 1;
        }
      }
    }
  }

  const hideModal = () => dispatch(closeDialog());
  const sourceUniverse = sourceUniverseQuery.data;
  const targetUniverse = targetUniverseQuery.data;
  const numTablesRequiringBootstrap = xClusterConfig.tableDetails.reduce(
    (errorCount: number, xClusterTableDetails) => {
      return xClusterTableDetails.status === XClusterTableStatus.ERROR
        ? errorCount + 1
        : errorCount;
    },
    0
  );
  const enabledConfigActions = getEnabledConfigActions(xClusterConfig);

  const shouldShowConfigError = numTablesRequiringBootstrap > 0;
  const shouldShowTableLagWarning =
    maxAcceptableLagQuery.isSuccess &&
    numTablesAboveLagThreshold > 0 &&
    xClusterConfigTableUUIDs.length > 0;
  const isAddTableModalVisible =
    showModal && visibleModal === XClusterModalName.ADD_TABLE_TO_CONFIG;
  const isEditConfigModalVisible = showModal && visibleModal === XClusterModalName.EDIT_CONFIG;
  const isDeleteConfigModalVisible = showModal && visibleModal === XClusterModalName.DELETE_CONFIG;
  const isRestartConfigModalVisible =
    showModal && visibleModal === XClusterModalName.RESTART_CONFIG;
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
                  btnClass={'btn btn-orange replication-status-button'}
                  disabled={
                    !_.includes(
                      enabledConfigActions,
                      xClusterConfig.paused
                        ? XClusterConfigAction.RESUME
                        : XClusterConfigAction.PAUSE
                    )
                  }
                  onClick={() => {
                    toast.success('Please wait...');
                    toggleConfigPausedState.mutateAsync(xClusterConfig);
                  }}
                />
                <ButtonGroup className="more-actions-button">
                  <DropdownButton pullRight id="alert-mark-as-button" title="Actions">
                    <MenuItem
                      eventKey="1"
                      onClick={(e) => {
                        if (_.includes(enabledConfigActions, XClusterConfigAction.EDIT)) {
                          dispatch(openDialog(XClusterModalName.EDIT_CONFIG));
                        }
                      }}
                      disabled={!_.includes(enabledConfigActions, XClusterConfigAction.EDIT)}
                    >
                      <YBLabelWithIcon className="xCluster-dropdown-button" icon="fa fa-pencil">
                        Edit Replication Name
                      </YBLabelWithIcon>
                    </MenuItem>
                    <MenuItem
                      eventKey="2"
                      onClick={() => {
                        if (_.includes(enabledConfigActions, XClusterConfigAction.RESTART)) {
                          dispatch(openDialog(XClusterModalName.RESTART_CONFIG));
                        }
                      }}
                      disabled={!_.includes(enabledConfigActions, XClusterConfigAction.RESTART)}
                    >
                      <YBLabelWithIcon className="xCluster-dropdown-button" icon="fa fa-refresh">
                        Restart Replication
                      </YBLabelWithIcon>
                    </MenuItem>
                    <MenuItem divider />
                    <MenuItem
                      eventKey="3"
                      onClick={() => {
                        if (_.includes(enabledConfigActions, XClusterConfigAction.DELETE)) {
                          dispatch(openDialog(XClusterModalName.DELETE_CONFIG));
                        }
                      }}
                      disabled={!_.includes(enabledConfigActions, XClusterConfigAction.DELETE)}
                    >
                      <YBLabelWithIcon className="xCluster-dropdown-button" icon="fa fa-times">
                        Delete Replication
                      </YBLabelWithIcon>
                    </MenuItem>
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
                  xClusterConfigTableUUIDs.length
                } ${xClusterConfigTableUUIDs.length > 1 ? 'tables' : 'table'} ${
                  numTablesAboveLagThreshold > 1 ? 'have' : 'has'
                }
                exceeded the maximum acceptable lag time.`}
              </YBBanner>
            )}
          </div>
          <Row className="replication-status">
            <Col lg={4}>
              Replication Status <XClusterConfigStatusLabel xClusterConfig={xClusterConfig} />
            </Col>
            <Col lg={8} className="lag-status-graph">
              <div className="lag-stats">
                <Row>
                  <Col lg={6}>Current Lag</Col>
                  <Col lg={6}>
                    <span className="lag-text">
                      <CurrentReplicationLag
                        replicationUUID={xClusterConfig.uuid}
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
                      replication={xClusterConfig}
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
        {isAddTableModalVisible && (
          <AddTableModal
            onHide={hideModal}
            isVisible={isAddTableModalVisible}
            xClusterConfig={xClusterConfig}
            configTableType={configTableType}
          />
        )}
        {isEditConfigModalVisible && (
          <EditConfigModal
            replication={xClusterConfig}
            visible={isEditConfigModalVisible}
            onHide={hideModal}
          />
        )}
        {isDeleteConfigModalVisible && (
          <DeleteConfigModal
            sourceUniverseUUID={xClusterConfig.sourceUniverseUUID}
            targetUniverseUUID={xClusterConfig.targetUniverseUUID}
            xClusterConfig={xClusterConfig}
            onHide={hideModal}
            visible={showModal && visibleModal === XClusterModalName.DELETE_CONFIG}
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
      </div>
    </>
  );
}

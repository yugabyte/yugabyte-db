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
  isCatchUpBootstrapRequired
} from '../../../actions/xClusterReplication';
import { YBButton } from '../../common/forms/fields';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { YBTabsPanel } from '../../panels';
import { ReplicationContainer } from '../../tables';
import { Metrics, XClusterConfig } from '../XClusterTypes';
import {
  MetricName,
  MetricTraceName,
  ReplicationAction,
  REPLICATION_LAG_ALERT_NAME,
  TRANSITORY_STATES,
  XClusterConfigState,
  XClusterModalName,
  XCLUSTER_CONFIG_REFETCH_INTERVAL_MS
} from '../constants';
import {
  MaxAcceptableLag,
  CurrentReplicationLag,
  getEnabledConfigActions,
  parseFloatIfDefined
} from '../ReplicationUtils';
import { AddTablesToClusterModal } from './AddTablesToClusterModal';
import { EditReplicationDetails } from './EditReplicationDetails';
import { LagGraph } from './LagGraph';
import { ReplicationTables } from './ReplicationTables';
import { ReplicationOverview } from './ReplicationOverview';
import { XClusterConfigStatusLabel } from '../XClusterConfigStatusLabel';
import { DeleteConfigModal } from './DeleteConfigModal';
import { RestartConfigModal } from '../restartConfig/RestartConfigModal';
import { YBBanner, YBBannerVariant, YBLabelWithIcon } from '../../common/descriptors';
import { api } from '../../../redesign/helpers/api';
import { getAlertConfigurations } from '../../../actions/universe';

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
  params: { uuid: currentUniverseUUID, replicationUUID }
}: Props) {
  const { showModal, visibleModal } = useSelector((state: any) => state.modal);
  const dispatch = useDispatch();
  const queryClient = useQueryClient();

  const xClusterConfigQuery = useQuery(['Xcluster', replicationUUID], () =>
    fetchXClusterConfig(replicationUUID)
  );
  const sourceUniverseQuery = useQuery(
    ['universe', xClusterConfigQuery.data?.sourceUniverseUUID],
    () => api.fetchUniverse(xClusterConfigQuery.data?.sourceUniverseUUID),
    { enabled: !!xClusterConfigQuery.data }
  );

  const targetUniverseQuery = useQuery(
    ['universe', xClusterConfigQuery.data?.sourceUniverseUUID],
    () => api.fetchUniverse(xClusterConfigQuery.data?.targetUniverseUUID),
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

  const countBootstrapRequiredTablesQuery = useQuery(
    ['Xcluster', 'needBootstrap', xClusterConfigQuery.data?.tables, 'count'],
    () =>
      isCatchUpBootstrapRequired(
        xClusterConfigQuery.data?.uuid,
        xClusterConfigQuery.data?.tables
      ).then((bootstrapTests) => {
        let numTablesRequiringBootstrap = 0;
        for (const bootstrapTest of bootstrapTests) {
          // Each bootstrapTest response is of the form {<tableUUID>: boolean}.
          // Until the backend supports multiple tableUUIDs per request, the response object
          const tableUUID = Object.keys(bootstrapTest)[0];

          if (bootstrapTest[tableUUID]) {
            numTablesRequiringBootstrap += 1;
          }
        }
        return numTablesRequiringBootstrap;
      }),
    { enabled: !!xClusterConfigQuery.data }
  );

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
    if (_.includes(TRANSITORY_STATES, xClusterConfig?.status)) {
      queryClient.invalidateQueries(['Xcluster', xClusterConfig.uuid]);
    }
  }, XCLUSTER_CONFIG_REFETCH_INTERVAL_MS);

  if (
    xClusterConfigQuery.isLoading ||
    xClusterConfigQuery.isIdle ||
    sourceUniverseQuery.isLoading ||
    sourceUniverseQuery.isIdle ||
    targetUniverseQuery.isLoading ||
    targetUniverseQuery.isIdle
  ) {
    return <YBLoading />;
  }

  if (xClusterConfigQuery.isError || sourceUniverseQuery.isError || targetUniverseQuery.isError) {
    return <YBErrorIndicator />;
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
  const xClusterConfig = xClusterConfigQuery.data;
  const sourceUniverse = sourceUniverseQuery.data;
  const targetUniverse = targetUniverseQuery.data;
  const numTablesRequiringBootstrap = countBootstrapRequiredTablesQuery.data ?? 0;
  const enabledConfigActions = getEnabledConfigActions(xClusterConfig);

  const shouldShowConfigError = numTablesRequiringBootstrap > 0;
  const shouldShowTableLagWarning =
    maxAcceptableLagQuery.isSuccess &&
    numTablesAboveLagThreshold > 0 &&
    xClusterConfigTableUUIDs.length > 0;
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
                      xClusterConfig.paused ? ReplicationAction.RESUME : ReplicationAction.PAUSE
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
                        if (_.includes(enabledConfigActions, ReplicationAction.EDIT)) {
                          dispatch(openDialog(XClusterModalName.EDIT_CONFIG));
                        }
                      }}
                      disabled={!_.includes(enabledConfigActions, ReplicationAction.EDIT)}
                    >
                      <YBLabelWithIcon className="xCluster-dropdown-button" icon="fa fa-pencil">
                        Edit Replication Name
                      </YBLabelWithIcon>
                    </MenuItem>
                    <MenuItem
                      eventKey="2"
                      onClick={() => {
                        if (_.includes(enabledConfigActions, ReplicationAction.RESTART)) {
                          dispatch(openDialog(XClusterModalName.RESTART_CONFIG));
                        }
                      }}
                      disabled={!_.includes(enabledConfigActions, ReplicationAction.RESTART)}
                    >
                      <YBLabelWithIcon className="xCluster-dropdown-button" icon="fa fa-refresh">
                        Restart Replication
                      </YBLabelWithIcon>
                    </MenuItem>
                    <MenuItem divider />
                    <MenuItem
                      eventKey="3"
                      onClick={() => {
                        if (_.includes(enabledConfigActions, ReplicationAction.DELETE)) {
                          dispatch(openDialog(XClusterModalName.DELETE_CONFIG));
                        }
                      }}
                      disabled={!_.includes(enabledConfigActions, ReplicationAction.DELETE)}
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
                      if (_.includes(enabledConfigActions, ReplicationAction.RESTART)) {
                        dispatch(openDialog(XClusterModalName.RESTART_CONFIG));
                      }
                    }}
                    disabled={!_.includes(enabledConfigActions, ReplicationAction.RESTART)}
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
                  <ReplicationTables replication={xClusterConfig} />
                </Tab>
                <Tab eventKey={'metrics'} title="Metrics" id="universe-tab-panel">
                  <ReplicationContainer
                    sourceUniverseUUID={xClusterConfig.sourceUniverseUUID}
                    hideHeader={true}
                    replicationUUID={replicationUUID}
                  />
                </Tab>
              </YBTabsPanel>
            </Col>
          </Row>
        </div>
        <AddTablesToClusterModal
          onHide={hideModal}
          visible={showModal && visibleModal === XClusterModalName.ADD_TABLE_TO_CONFIG}
          replication={xClusterConfig}
        />
        <EditReplicationDetails
          replication={xClusterConfig}
          visible={showModal && visibleModal === XClusterModalName.EDIT_CONFIG}
          onHide={hideModal}
        />
        <DeleteConfigModal
          currentUniverseUUID={currentUniverseUUID}
          xClusterConfig={xClusterConfig}
          onHide={hideModal}
          visible={showModal && visibleModal === XClusterModalName.DELETE_CONFIG}
        />
        <RestartConfigModal
          onHide={hideModal}
          isVisible={showModal && visibleModal === XClusterModalName.RESTART_CONFIG}
          currentUniverseUUID={currentUniverseUUID}
          xClusterConfigUUID={replicationUUID}
        />
      </div>
    </>
  );
}

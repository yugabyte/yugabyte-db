import React, { useState } from 'react';
import { ButtonGroup, Col, DropdownButton, MenuItem, Row, Tab } from 'react-bootstrap';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { useDispatch, useSelector } from 'react-redux';
import { Link } from 'react-router';
import { toast } from 'react-toastify';
import { useInterval, useMount } from 'react-use';
import _ from 'lodash';

import { closeDialog, openDialog } from '../../../actions/modal';
import { fetchUniverseList } from '../../../actions/universe';
import {
  getXclusterConfig,
  fetchTaskUntilItCompletes,
  editXClusterState
} from '../../../actions/xClusterReplication';
import { YBButton } from '../../common/forms/fields';
import { YBLoading } from '../../common/indicators';
import { YBTabsPanel } from '../../panels';
import { ReplicationContainer } from '../../tables';
import { XClusterConfig } from '../XClusterTypes';
import {
  ReplicationAction,
  TRANSITORY_STATES,
  XClusterConfigState,
  XClusterModalName
} from '../constants';
import {
  findUniverseName,
  GetConfiguredThreshold,
  GetCurrentLag,
  getEnabledConfigActions
} from '../ReplicationUtils';
import { AddTablesToClusterModal } from './AddTablesToClusterModal';
import { EditReplicationDetails } from './EditReplicationDetails';
import { LagGraph } from './LagGraph';
import { ReplicationTables } from './ReplicationTables';
import { ReplicationOverview } from './ReplicationOverview';
import { XClusterConfigStatusLabel } from '../XClusterConfigStatusLabel';
import { DeleteConfigModal } from './DeleteConfigModal';
import { RestartConfigModal } from '../restartConfig/RestartConfigModal';

import './ReplicationDetails.scss';

interface Props {
  params: {
    uuid: string;
    replicationUUID: string;
  };
}

export function ReplicationDetails({ params }: Props) {
  const { showModal, visibleModal } = useSelector((state: any) => state.modal);
  const [universesList, setUniversesList] = useState([]);
  const dispatch = useDispatch();
  const queryClient = useQueryClient();
  const hideModal = () => dispatch(closeDialog());

  const {
    data: replication,
    isLoading
  }: { data: XClusterConfig | undefined; isLoading: boolean } = useQuery(
    ['Xcluster', params.replicationUUID],
    () => getXclusterConfig(params.replicationUUID)
  );

  useMount(async () => {
    const resp = await dispatch(fetchUniverseList());
    setUniversesList((await resp.payload).data);
  });

  //refresh metrics for every 20 seconds
  useInterval(() => {
    queryClient.invalidateQueries('xcluster-metric');
    if (_.includes(TRANSITORY_STATES, replication?.status)) {
      queryClient.invalidateQueries('Xcluster');
    }
  }, 20_000);

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

  if (isLoading || universesList.length === 0 || !replication) {
    return <YBLoading />;
  }

  const getUniverseByUUID = (uuid: string) => {
    return universesList.filter((universes: any) => universes.universeUUID === uuid)[0];
  };

  const destinationUniverse = getUniverseByUUID(replication.targetUniverseUUID);

  return (
    <>
      <div className="replication-details">
        <h2 className="content-title">
          <Link to={`/universes/${replication.sourceUniverseUUID}`}>
            {findUniverseName(universesList, replication.sourceUniverseUUID)}
          </Link>
          <span className="subtext">
            <i className="fa fa-chevron-right submenu-icon" />
            <Link to={`/universes/${replication.sourceUniverseUUID}/replication/`}>
              Replication
            </Link>
            <i className="fa fa-chevron-right submenu-icon" />
            {replication.name}
          </span>
        </h2>
        <div className="details-canvas">
          <Row className="replication-actions">
            <Col lg={7}>
              <h3>{replication.name}</h3>
            </Col>
            <Col lg={5} className="noPadding">
              <Row className="details-actions-button">
                <YBButton
                  btnText={`${replication.paused ? 'Enable' : 'Pause'} Replication`}
                  btnClass={'btn btn-orange replication-status-button'}
                  disabled={
                    !_.includes(
                      getEnabledConfigActions(replication),
                      replication.paused ? ReplicationAction.RESUME : ReplicationAction.PAUSE
                    )
                  }
                  onClick={() => {
                    toast.success('Please wait...');
                    toggleConfigPausedState.mutateAsync(replication);
                  }}
                />
                <ButtonGroup className="more-actions-button">
                  <DropdownButton pullRight id="alert-mark-as-button" title="Actions">
                    <MenuItem
                      eventKey="1"
                      onClick={(e) => {
                        if (
                          _.includes(getEnabledConfigActions(replication), ReplicationAction.EDIT)
                        ) {
                          dispatch(openDialog(XClusterModalName.EDIT_CONFIG));
                        }
                      }}
                      disabled={
                        !_.includes(getEnabledConfigActions(replication), ReplicationAction.EDIT)
                      }
                    >
                      Edit Replication Configurations
                    </MenuItem>
                    <MenuItem
                      eventKey="2"
                      onClick={() => {
                        if (
                          _.includes(getEnabledConfigActions(replication), ReplicationAction.DELETE)
                        ) {
                          dispatch(openDialog(XClusterModalName.DELETE_CONFIG));
                        }
                      }}
                      disabled={
                        !_.includes(getEnabledConfigActions(replication), ReplicationAction.DELETE)
                      }
                    >
                      Delete Replication
                    </MenuItem>
                    <MenuItem
                      eventKey="3"
                      onClick={() => {
                        if (
                          _.includes(
                            getEnabledConfigActions(replication),
                            ReplicationAction.RESTART
                          )
                        ) {
                          dispatch(openDialog(XClusterModalName.RESTART_CONFIG));
                        }
                      }}
                      disabled={
                        !_.includes(getEnabledConfigActions(replication), ReplicationAction.RESTART)
                      }
                    >
                      Restart Replication
                    </MenuItem>
                  </DropdownButton>
                </ButtonGroup>
              </Row>
            </Col>
          </Row>
          <Row className="replication-status">
            <Col lg={4}>
              Replication Status <XClusterConfigStatusLabel xClusterConfig={replication} />
            </Col>
            <Col lg={8} className="lag-status-graph">
              <div className="lag-stats">
                <Row>
                  <Col lg={6}>Current Lag</Col>
                  <Col lg={6}>
                    <span className="lag-text">
                      <GetCurrentLag
                        replicationUUID={replication.uuid}
                        sourceUniverseUUID={replication.sourceUniverseUUID}
                      />
                    </span>
                  </Col>
                </Row>
                <div className="replication-divider" />
                <Row>
                  <Col lg={6}>Max acceptable lag</Col>
                  <Col lg={6}>
                    <span className="lag-value">
                      <GetConfiguredThreshold
                        currentUniverseUUID={replication.sourceUniverseUUID}
                      />
                    </span>
                  </Col>
                </Row>
              </div>
              <div>
                <LagGraph
                  replicationUUID={replication.uuid}
                  sourceUniverseUUID={replication.sourceUniverseUUID}
                />
              </div>
            </Col>
          </Row>
          <Row className="replication-details-panel noPadding">
            <Col lg={12} className="noPadding">
              <YBTabsPanel defaultTab={'overview'} id="replication-tab-panel">
                <Tab eventKey={'overview'} title={'Overview'}>
                  {destinationUniverse !== undefined && (
                    <ReplicationOverview
                      replication={replication}
                      destinationUniverse={destinationUniverse}
                    />
                  )}
                </Tab>
                <Tab eventKey={'tables'} title={'Tables'}>
                  <ReplicationTables replication={replication} />
                </Tab>
                <Tab eventKey={'metrics'} title="Metrics" id="universe-tab-panel">
                  <ReplicationContainer
                    sourceUniverseUUID={replication.sourceUniverseUUID}
                    hideHeader={true}
                    replicationUUID={params.replicationUUID}
                  />
                </Tab>
              </YBTabsPanel>
            </Col>
          </Row>
        </div>
        <AddTablesToClusterModal
          onHide={hideModal}
          visible={showModal && visibleModal === XClusterModalName.ADD_TABLE_TO_CONFIG}
          replication={replication}
        />
        <EditReplicationDetails
          replication={replication}
          visible={showModal && visibleModal === XClusterModalName.EDIT_CONFIG}
          onHide={hideModal}
        />
        <DeleteConfigModal
          currentUniverseUUID={params.uuid}
          xClusterConfig={replication}
          onHide={hideModal}
          visible={showModal && visibleModal === XClusterModalName.DELETE_CONFIG}
        />
        <RestartConfigModal
          onHide={hideModal}
          isVisible={showModal && visibleModal === XClusterModalName.RESTART_CONFIG}
          currentUniverseUUID={params.uuid}
          xClusterConfigUUID={replication.uuid}
        />
      </div>
    </>
  );
}

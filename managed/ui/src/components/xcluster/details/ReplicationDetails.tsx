import React, { useState } from 'react';
import { ButtonGroup, Col, DropdownButton, MenuItem, Row, Tab } from 'react-bootstrap';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { useDispatch, useSelector } from 'react-redux';
import { Link } from 'react-router';
import { useMount } from 'react-use';
import { IReplicationStatus } from '..';
import { closeDialog, openDialog } from '../../../actions/modal';
import { fetchUniverseList } from '../../../actions/universe';
import {
  getXclusterConfig,
  changeXClusterStatus,
  deleteXclusterConfig
} from '../../../actions/xClusterReplication';
import { YBButton } from '../../common/forms/fields';
import { YBLoading } from '../../common/indicators';
import { YBTabsPanel } from '../../panels';
import { ReplicationAlertModalBtn } from '../../tables/Replication/ReplicationAlertModalBtn';
import { IReplication } from '../IClusterReplication';
import { GetConfiguredThreshold, GetCurrentLag, getReplicationStatus } from '../ReplicationUtils';
import { AddTablesToClusterModal } from './AddTablesToClusterModal';
import { EditReplicationDetails } from './EditReplicationDetails';

import './ReplicationDetails.scss';
import { ReplicationDetailsTable } from './ReplicationDetailsTable';
import { ReplicationOverview } from './ReplicationOverview';

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
  }: { data: IReplication | undefined; isLoading: boolean } = useQuery(
    ['Xcluster', params.replicationUUID],
    () => getXclusterConfig(params.replicationUUID)
  );

  useMount(async () => {
    const resp = await dispatch(fetchUniverseList());
    setUniversesList((await resp.payload).data);
  });

  const switchReplicationStatus = useMutation((replication: IReplication) => {
    changeXClusterStatus(
      replication,
      replication.status === IReplicationStatus.PAUSED
        ? IReplicationStatus.RUNNING
        : IReplicationStatus.PAUSED
    );
    return queryClient.invalidateQueries(['Xcluster', replication.uuid]);
  });

  const deleteReplication = useMutation((uuid: string) => {
    return deleteXclusterConfig(uuid).then(() => {
      window.location.href = `/universes/${replication?.sourceUniverseUUID}/replication`;
    });
  });

  if (isLoading || universesList.length === 0 || !replication) {
    return <YBLoading />;
  }

  const getUniverseByUUID = (uuid: string) => {
    return universesList.filter((universes: any) => universes.universeUUID === uuid)[0];
  };

  const sourceUniverse: any = getUniverseByUUID(replication.sourceUniverseUUID);
  const destinationUniverse = getUniverseByUUID(replication.targetUniverseUUID);

  return (
    <>
      <div className="replication-details">
        <h2 className="content-title">
          <Link to={`/universes/${params.uuid}`}>{sourceUniverse.name}</Link>
          <span className="subtext">
            <i className="fa fa-chevron-right submenu-icon" />
            <Link to={`/universes/${params.uuid}/replication/`}>Replication</Link>
            <i className="fa fa-chevron-right submenu-icon" />
            {replication.name}
          </span>
        </h2>
        <div className="details-canvas">
          <Row className="replication-actions">
            <Col lg={7}>
              <h3>{replication.name}</h3>
            </Col>
            <Col lg={4} lgPush={2}>
              <YBButton
                btnText={`${
                  replication.status === IReplicationStatus.RUNNING ? 'Pause' : 'Enable'
                } Replication`}
                btnClass={'btn btn-orange replication-status-button'}
                disabled={
                  replication.status === IReplicationStatus.FAILED ||
                  replication.status === IReplicationStatus.INIT
                }
                onClick={() => {
                  switchReplicationStatus.mutateAsync(replication);
                }}
              />
              <ReplicationAlertModalBtn
                universeUUID={replication.sourceUniverseUUID}
                disabled={replication.status !== IReplicationStatus.RUNNING}
              />
              <ButtonGroup className="more-actions-button">
                <DropdownButton pullRight id="alert-mark-as-button" title="Actions">
                  <MenuItem
                    eventKey="1"
                    onClick={(e) => {
                      dispatch(openDialog('editReplicationConfiguration'));
                    }}
                  >
                    Edit replication configurations
                  </MenuItem>
                  <MenuItem
                    eventKey="2"
                    onClick={(e) => {
                      deleteReplication.mutateAsync(replication.uuid);
                    }}
                  >
                    Delete replication
                  </MenuItem>
                </DropdownButton>
              </ButtonGroup>
            </Col>
          </Row>
          <Row className="replication-status">
            <Col lg={4}>Replication Status {getReplicationStatus(replication.status)}</Col>
            <Col lg={8}>
              <Row>
                <Col lg={2}>Current Lag</Col>
                <Col lg={6}>
                  <span className="lag-text">
                    <GetCurrentLag
                      replicationUUID={replication.uuid}
                      sourceUniverseUUID={replication.sourceUniverseUUID}
                    />
                  </span>
                  <span> ms</span>
                </Col>
              </Row>
              <div className="replication-divider" />
              <Row>
                <Col lg={2}>Max acceptable lag</Col>
                <Col lg={6}>
                  <span className="lag-value">
                    <GetConfiguredThreshold currentUniverseUUID={replication.sourceUniverseUUID} />
                  </span>{' '}
                  ms
                </Col>
              </Row>
            </Col>
          </Row>
          <Row className="replication-details-panel noPadding">
            <Col lg={12} className="noPadding">
              <YBTabsPanel defaultTab={'overview'} id="replication-tab-panel">
                <Tab eventKey={'overview'} title={'Overview'}>
                  <ReplicationOverview
                    replication={replication}
                    destinationUniverse={destinationUniverse}
                  />
                </Tab>
                <Tab eventKey={'tables'} title={'Tables'}>
                  <ReplicationDetailsTable replication={replication} />
                </Tab>
              </YBTabsPanel>
            </Col>
          </Row>
        </div>
        <AddTablesToClusterModal
          onHide={hideModal}
          visible={showModal && visibleModal === 'addTablesToClusterModal'}
          replication={replication}
        />
        <EditReplicationDetails
          replication={replication}
          visible={showModal && visibleModal === 'editReplicationConfiguration'}
          onHide={hideModal}
        />
      </div>
    </>
  );
}

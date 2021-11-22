import React from 'react';
import { Col, Row } from 'react-bootstrap';
import { useDispatch, useSelector } from 'react-redux';
import { closeDialog, openDialog } from '../../actions/modal';
import { YBButton } from '../common/forms/fields';
import { ConfigureReplicationModal } from './ConfigureReplicationModal';

import { ReplicationList } from './ReplicationList';

export default function Replication({ currentUniverseUUID }: { currentUniverseUUID: string }) {
  const dispatch = useDispatch();
  const { showModal, visibleModal } = useSelector((state: any) => state.modal);

  const showAddClusterReplicationModal = () => {
    dispatch(openDialog('addClusterReplicationModal'));
  };

  const hideModal = () => dispatch(closeDialog());

  return (
    <>
      <Row>
        <Col lg={6}>
          <h3>Replication</h3>
        </Col>
        <Col lg={6}>
          <Row className="cluster_action">
            <Col lg={9} className="cluster_support_text">
              <i className="fa fa-exclamation-circle"></i> For replicating a source universe with
              existing data, please{' '}
              <a href="https://support.yugabyte.com/hc/en-us">contact support</a>
            </Col>
            <Col lg={3}>
              <YBButton
                btnText="Configure replication"
                btnClass={'btn btn-orange'}
                onClick={showAddClusterReplicationModal}
              />
            </Col>
          </Row>
        </Col>
      </Row>
      <Row className="replication-list">
        <Col lg={12}>
          <ReplicationList currentUniverseUUID={currentUniverseUUID} />
          <ConfigureReplicationModal
            currentUniverseUUID={currentUniverseUUID}
            onHide={hideModal}
            visible={showModal && visibleModal === 'addClusterReplicationModal'}
          />
        </Col>
      </Row>
    </>
  );
}

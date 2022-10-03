import React from 'react';
import { Col, Row } from 'react-bootstrap';
import { useDispatch, useSelector } from 'react-redux';
import clsx from 'clsx';

import { closeDialog, openDialog } from '../../actions/modal';
import { YBButton } from '../common/forms/fields';
import { ConfigureMaxLagTimeModal } from './ConfigureMaxLagTimeModal';
import { CreateConfigModal } from './createConfig/CreateConfigModal';
import { XClusterConfigList } from './XClusterConfigList';

import styles from './Replication.module.scss';

export default function Replication({ currentUniverseUUID }: { currentUniverseUUID: string }) {
  const dispatch = useDispatch();
  const { showModal, visibleModal } = useSelector((state: any) => state.modal);

  const showAddClusterReplicationModal = () => {
    dispatch(openDialog('addClusterReplicationModal'));
  };

  const showConfigureMaxLagTimeModal = () => {
    dispatch(openDialog('configureMaxLagTimeModal'));
  };

  const hideModal = () => dispatch(closeDialog());

  return (
    <>
      <Row>
        <Col lg={6}>
          <h3>Replication</h3>
        </Col>
        <Col lg={6}>
          <Row className={styles.configActionsContainer}>
            <Row>
              <YBButton
                btnText="Max acceptable lag time"
                btnClass={clsx('btn', styles.setMaxAcceptableLagBtn)}
                btnIcon="fa fa-bell-o"
                onClick={showConfigureMaxLagTimeModal}
              />
              <YBButton
                btnText="Configure Replication"
                btnClass={'btn btn-orange'}
                onClick={showAddClusterReplicationModal}
              />
            </Row>
          </Row>
        </Col>
      </Row>
      <Row>
        <Col lg={12}>
          <XClusterConfigList currentUniverseUUID={currentUniverseUUID} />
          <CreateConfigModal
            currentUniverseUUID={currentUniverseUUID}
            onHide={hideModal}
            visible={showModal && visibleModal === 'addClusterReplicationModal'}
          />
          <ConfigureMaxLagTimeModal
            visible={showModal && visibleModal === 'configureMaxLagTimeModal'}
            // visible={true}
            currentUniverseUUID={currentUniverseUUID}
            onHide={hideModal}
          />
        </Col>
      </Row>
    </>
  );
}

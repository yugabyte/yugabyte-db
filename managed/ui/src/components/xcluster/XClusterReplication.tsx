import clsx from 'clsx';
import { Col, Row } from 'react-bootstrap';
import { useDispatch, useSelector } from 'react-redux';
import { useQuery } from 'react-query';

import { closeDialog, openDialog } from '../../actions/modal';
import { YBButton } from '../common/forms/fields';
import { ConfigureMaxLagTimeModal } from './ConfigureMaxLagTimeModal';
import { CreateConfigModal } from './createConfig/CreateConfigModal';
import { XClusterConfigList } from './XClusterConfigList';
import { api } from '../../redesign/helpers/api';
import { YBErrorIndicator, YBLoading } from '../common/indicators';
import { getUniverseStatus } from '../universes/helpers/universeHelpers';
import { UnavailableUniverseStates } from '../../redesign/helpers/constants';

import { Universe } from '../../redesign/helpers/dtos';

import { RbacValidator } from '../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../redesign/features/rbac/ApiAndUserPermMapping';
import styles from './XClusterReplication.module.scss';

export const XClusterReplication = ({ currentUniverseUUID }: { currentUniverseUUID: string }) => {
  const dispatch = useDispatch();
  const { showModal, visibleModal } = useSelector((state: any) => state.modal);

  const universeQuery = useQuery<Universe>(['universe', currentUniverseUUID], () =>
    api.fetchUniverse(currentUniverseUUID)
  );

  if (universeQuery.isLoading || universeQuery.isIdle) {
    return <YBLoading />;
  }

  if (universeQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={`Error fetching universe with UUID: ${currentUniverseUUID}`}
      />
    );
  }

  const showAddClusterReplicationModal = () => {
    dispatch(openDialog('addClusterReplicationModal'));
  };
  const showConfigureMaxLagTimeModal = () => {
    dispatch(openDialog('configureMaxLagTimeModal'));
  };

  const hideModal = () => dispatch(closeDialog());

  const shouldDisableXClusterActions = UnavailableUniverseStates.includes(
    getUniverseStatus(universeQuery.data).state
  );

  return (
    <>
      <Row>
        <Col lg={6}>
          <h3>Replication</h3>
        </Col>
        <Col lg={6}>
          <Row className={styles.configActionsContainer}>
            <Row>
              <RbacValidator
                accessRequiredOn={ApiPermissionMap.CREATE_ALERT_CONFIGURATIONS}
                isControl
              >
                <YBButton
                  btnText="Max acceptable lag time"
                  btnClass={clsx('btn', styles.setMaxAcceptableLagBtn)}
                  btnIcon="fa fa-bell-o"
                  onClick={showConfigureMaxLagTimeModal}
                />
              </RbacValidator>
              <RbacValidator
                accessRequiredOn={{
                  ...ApiPermissionMap.CREATE_XCLUSTER_REPLICATION,
                  onResource: { UNIVERSE: currentUniverseUUID },
                }}
                isControl
              >
                <YBButton
                  btnText="Configure Replication"
                  btnClass={'btn btn-orange'}
                  onClick={showAddClusterReplicationModal}
                  disabled={shouldDisableXClusterActions}
                />
              </RbacValidator>
            </Row>
          </Row>
        </Col>
      </Row>
      <Row>
        <Col lg={12}>
          <XClusterConfigList currentUniverseUUID={currentUniverseUUID} />
          <CreateConfigModal
            sourceUniverseUUID={currentUniverseUUID}
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
};

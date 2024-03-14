import clsx from 'clsx';
import { Col, Row } from 'react-bootstrap';
import { useDispatch, useSelector } from 'react-redux';
import { useQueries, useQuery, UseQueryResult } from 'react-query';
import { useTranslation } from 'react-i18next';
import { Typography } from '@material-ui/core';

import { closeDialog, openDialog } from '../../actions/modal';
import { YBButton } from '../common/forms/fields';
import { CreateConfigModal } from './createConfig/CreateConfigModal';
import { XClusterConfigList } from './XClusterConfigList';
import { api, xClusterQueryKey } from '../../redesign/helpers/api';
import { YBErrorIndicator, YBLoading } from '../common/indicators';
import { getUniverseStatus } from '../universes/helpers/universeHelpers';
import { UnavailableUniverseStates } from '../../redesign/helpers/constants';
import { getXClusterConfigUuids } from './ReplicationUtils';
import { fetchXClusterConfig } from '../../actions/xClusterReplication';
import { XClusterConfigType } from './constants';
import { RbacValidator } from '../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../redesign/features/rbac/ApiAndUserPermMapping';
import { YBTooltip } from '../../redesign/components';

import { Universe } from '../../redesign/helpers/dtos';
import { XClusterConfig } from './dtos';

import styles from './XClusterReplication.module.scss';

const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster';

export const XClusterReplication = ({ currentUniverseUUID }: { currentUniverseUUID: string }) => {
  const dispatch = useDispatch();
  const { showModal, visibleModal } = useSelector((state: any) => state.modal);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const universeQuery = useQuery<Universe>(['universe', currentUniverseUUID], () =>
    api.fetchUniverse(currentUniverseUUID)
  );
  const { sourceXClusterConfigUuids, targetXClusterConfigUuids } = getXClusterConfigUuids(
    universeQuery.data
  );

  // List the XCluster Configurations for which the current universe is a source or a target.
  const universeXClusterConfigUUIDs: string[] = [
    ...sourceXClusterConfigUuids,
    ...targetXClusterConfigUuids
  ];
  // The unsafe cast is needed due to issue with useQueries typing
  // Upgrading react-query to v3.28 may solve this issue: https://github.com/TanStack/query/issues/1675
  const xClusterConfigQueries = useQueries(
    universeXClusterConfigUUIDs.map((uuid: string) => ({
      queryKey: xClusterQueryKey.detail(uuid),
      queryFn: () => fetchXClusterConfig(uuid),
      enabled: universeQuery.data?.universeDetails !== undefined
    }))
  ) as UseQueryResult<XClusterConfig>[];

  if (universeQuery.isLoading || universeQuery.isIdle) {
    return <YBLoading />;
  }

  if (universeQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('error.failedToFetchUniverse', { universeUuid: currentUniverseUUID })}
      />
    );
  }

  const showAddClusterReplicationModal = () => {
    dispatch(openDialog('addClusterReplicationModal'));
  };

  const hideModal = () => dispatch(closeDialog());

  const allowedTasks = universeQuery.data?.allowedTasks;
  const universeHasTxnXCluster = xClusterConfigQueries.some(
    (xClusterConfigQuery) => xClusterConfigQuery.data?.type === XClusterConfigType.TXN
  );
  const shouldDisableCreateXClusterConfig =
    UnavailableUniverseStates.includes(getUniverseStatus(universeQuery.data).state) ||
    universeHasTxnXCluster;
  return (
    <>
      <Row>
        <Col lg={6}>
          <Typography variant="h3">{t('heading')}</Typography>
        </Col>
        <Col lg={6}>
          <Row className={styles.configActionsContainer}>
            <Row>
              <RbacValidator
                accessRequiredOn={{
                  ...ApiPermissionMap.CREATE_XCLUSTER_REPLICATION,
                  onResource: { UNIVERSE: currentUniverseUUID }
                }}
                isControl
              >
                <YBTooltip
                  title={
                    shouldDisableCreateXClusterConfig
                      ? t('actionButton.createXClusterConfig.tooltip.universeLinkedToTxnXCluster')
                      : ''
                  }
                  placement="top"
                >
                  <span>
                    <YBButton
                      btnText={t('actionButton.createXClusterConfig.label')}
                      btnClass={'btn btn-orange'}
                      onClick={showAddClusterReplicationModal}
                      disabled={shouldDisableCreateXClusterConfig}
                    />
                  </span>
                </YBTooltip>
              </RbacValidator>
            </Row>
          </Row>
        </Col>
      </Row>
      <Row>
        <Col lg={12}>
          <XClusterConfigList currentUniverseUUID={currentUniverseUUID} />
          <CreateConfigModal
            allowedTasks={allowedTasks}
            sourceUniverseUUID={currentUniverseUUID}
            onHide={hideModal}
            visible={showModal && visibleModal === 'addClusterReplicationModal'}
          />
        </Col>
      </Row>
    </>
  );
};

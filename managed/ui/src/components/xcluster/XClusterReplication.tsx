import { useState } from 'react';
import { Col, Row } from 'react-bootstrap';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { Typography } from '@material-ui/core';

import { YBButton } from '../common/forms/fields';
import { XClusterConfigList } from './XClusterConfigList';
import { api } from '../../redesign/helpers/api';
import { YBErrorIndicator, YBLoading } from '../common/indicators';
import { getUniverseStatus } from '../universes/helpers/universeHelpers';
import { UnavailableUniverseStates, UNIVERSE_TASKS } from '../../redesign/helpers/constants';
import { RbacValidator } from '../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../redesign/features/rbac/ApiAndUserPermMapping';
import { YBTooltip } from '../../redesign/components';
import { CreateConfigModal } from './createConfig/CreateConfigModal';
import { isActionFrozen } from '../../redesign/helpers/utils';

import { Universe } from '../../redesign/helpers/dtos';

import styles from './XClusterReplication.module.scss';

const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster';

export const XClusterReplication = ({ currentUniverseUUID }: { currentUniverseUUID: string }) => {
  const [isCreateConfigModalOpen, setIsCreateConfigModalOpen] = useState<boolean>(false);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const universeQuery = useQuery<Universe>(['universe', currentUniverseUUID], () =>
    api.fetchUniverse(currentUniverseUUID)
  );

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

  const openCreateConfigModal = () => setIsCreateConfigModalOpen(true);
  const closeCreateConfigModal = () => setIsCreateConfigModalOpen(false);

  const allowedTasks = universeQuery.data?.allowedTasks;
  const shouldDisableCreateXClusterConfig =
    UnavailableUniverseStates.includes(getUniverseStatus(universeQuery.data).state) ||
    isActionFrozen(allowedTasks, UNIVERSE_TASKS.CONFIGURE_REPLICATION);
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
                      ? UnavailableUniverseStates.includes(
                          getUniverseStatus(universeQuery.data).state
                        )
                        ? t('actionButton.createXClusterConfig.tooltip.universeUnavailable')
                        : ''
                      : ''
                  }
                  placement="top"
                >
                  <span>
                    <YBButton
                      btnText={t('actionButton.createXClusterConfig.label')}
                      btnClass={'btn btn-orange'}
                      onClick={openCreateConfigModal}
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
        </Col>
      </Row>
      {isCreateConfigModalOpen && (
        <CreateConfigModal
          sourceUniverseUuid={currentUniverseUUID}
          modalProps={{ open: isCreateConfigModalOpen, onClose: closeCreateConfigModal }}
        />
      )}
    </>
  );
};

import { FC, useEffect, useState } from 'react';
import _ from 'lodash';
import { Col, DropdownButton, Grid, MenuItem, Row } from 'react-bootstrap';
import { AxiosError } from 'axios';
import { toast } from 'react-toastify';
import { Typography } from '@material-ui/core';
import { useMutation, useQueryClient } from 'react-query';
import { useTranslation } from 'react-i18next';

import { YBButton } from '../../common/forms/fields';
import { HaConfig, HaReplicationSchedule } from '../dtos';
import { HAErrorPlaceholder } from '../compounds/HAErrorPlaceholder';
import { DeleteModal } from '../modals/DeleteModal';
import { PromoteInstanceModal } from '../modals/PromoteInstanceModal';
import { FREQUENCY_MULTIPLIER } from './HAReplicationForm';
import { BadgeInstanceType } from '../compounds/BadgeInstanceType';
import { YBCopyButton } from '../../common/descriptors';
import YBInfoTip from '../../common/descriptors/YBInfoTip';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { api, QUERY_KEY } from '../../../redesign/helpers/api';
import { handleServerError } from '../../../utils/errorHandlingUtils';
import { YBMenuItemLabel } from '../../../redesign/components/YBDropdownMenu/YBMenuItemLabel';

import './HAReplicationView.scss';

interface DispatchProps {
  fetchRuntimeConfigs: () => void;
  setRuntimeConfig: (key: string, value: string) => void;
}

interface StateProps {
  runtimeConfigs: any;
}

interface OwnProps {
  haConfig: HaConfig;
  schedule: HaReplicationSchedule;
  editConfig(): void;
}

type HAReplicationViewProps = StateProps & DispatchProps & OwnProps;

export interface PeerCert {
  data: string;
  type: string;
}

const TRANSLATION_KEY_PREFIX = 'ha.config';
const COMPONENT_NAME = 'HaReplicationView';
export const HAReplicationView: FC<HAReplicationViewProps> = ({
  haConfig,
  schedule,
  runtimeConfigs,
  editConfig: enterEditMode,
  fetchRuntimeConfigs
}) => {
  const [isDeleteModalVisible, setDeleteModalVisible] = useState(false);
  const [isPromoteModalVisible, setPromoteModalVisible] = useState(false);
  const [isAddPeerCertsModalVisible, setAddPeerCertsModalVisible] = useState(false);
  const queryClient = useQueryClient();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const showDeleteModal = () => setDeleteModalVisible(true);
  const hideDeleteModal = () => setDeleteModalVisible(false);
  const showPromoteModal = () => setPromoteModalVisible(true);
  const hidePromoteModal = () => setPromoteModalVisible(false);

  useEffect(() => {
    fetchRuntimeConfigs();
  }, []); // eslint-disable-line react-hooks/exhaustive-deps

  const toggleCertificateValidationMutation = useMutation(
    () =>
      api.editHAConfig(haConfig.uuid, {
        cluster_key: haConfig.cluster_key,
        accept_any_certificate: !haConfig.accept_any_certificate
      }),
    {
      onSuccess: (response, values) => {
        queryClient.invalidateQueries(QUERY_KEY.getHAConfig);
        toast.success(
          <Typography variant="body2" component="span">
            {response.accept_any_certificate
              ? 'Disabled certificate validation.'
              : 'Enabled certificate validation.'}
          </Typography>
        );
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, { customErrorLabel: 'Failed to toggle certificate validation.' })
    }
  );
  const isRuntimeConfigLoaded = runtimeConfigs?.data && getPromiseState(runtimeConfigs).isSuccess();

  // sort by is_leader to show active instance on the very top, then sort other items by address
  const sortedInstances = _.sortBy(haConfig.instances, [(item) => !item.is_leader, 'address']);
  const currentInstance = sortedInstances.find((item) => item.is_local);
  if (currentInstance) {
    return (
      <Grid fluid className="ha-replication-view" data-testid="ha-replication-config-overview">
        <DeleteModal
          configId={haConfig.uuid}
          isStandby={!currentInstance.is_leader}
          visible={isDeleteModalVisible}
          onClose={hideDeleteModal}
        />
        <PromoteInstanceModal
          configId={haConfig.uuid}
          instanceId={currentInstance.uuid}
          visible={isPromoteModalVisible}
          onClose={hidePromoteModal}
        />

        <Row className="ha-replication-view__header">
          <Typography variant="h4">Overview</Typography>
          <div className="ha-replication-view__header-buttons">
            {currentInstance.is_leader ? (
              <>
                <DropdownButton
                  bsClass="dropdown"
                  title="Actions"
                  id={`${COMPONENT_NAME}-actionsDropdown`}
                  data-testid={`${COMPONENT_NAME}-actionsDropdown`}
                  pullRight
                >
                  <MenuItem onSelect={enterEditMode} name="Edit Configuartion">
                    <YBMenuItemLabel label="Edit Configuration" />
                  </MenuItem>
                  <MenuItem
                    onSelect={() => toggleCertificateValidationMutation.mutate()}
                    name={`${
                      haConfig.accept_any_certificate ? 'Enable' : 'Disable'
                    } Certificate Validation`}
                  >
                    <YBMenuItemLabel
                      label={`${
                        haConfig.accept_any_certificate ? 'Enable' : 'Disable'
                      } Certificate Validation`}
                    />
                  </MenuItem>
                  <MenuItem onSelect={showDeleteModal} name="Delete Configuration">
                    <YBMenuItemLabel label="Delete Configuration" />
                  </MenuItem>
                </DropdownButton>
              </>
            ) : (
              <>
                <YBInfoTip
                  placement="left"
                  title="Replication Configuration"
                  content="Promote this platform to active and demote other platforms to standby"
                />
                <YBButton
                  btnText="Make Active"
                  btnClass="btn btn-orange"
                  btnIcon="fa fa-upload"
                  onClick={showPromoteModal}
                />
                <YBButton btnText="Delete Configuration" onClick={showDeleteModal} />
              </>
            )}
          </div>
        </Row>
        <Row className="ha-replication-view__row">
          <Col xs={2} className="ha-replication-view__label">
            HA Global State
          </Col>
          <Col xs={10} className="ha-replication-view__value">
            {t(`globalState.${haConfig.global_state}`)}
          </Col>
        </Row>
        <Row className="ha-replication-view__row">
          <Col xs={2} className="ha-replication-view__label">
            Instance Type
          </Col>
          <Col xs={10} className="ha-replication-view__value">
            <BadgeInstanceType isActive={currentInstance.is_leader} />
          </Col>
        </Row>
        <Row className="ha-replication-view__row">
          <Col xs={2} className="ha-replication-view__label">
            IP Address / Hostname
          </Col>
          <Col xs={10} className="ha-replication-view__value">
            {currentInstance.address}
          </Col>
        </Row>
        <Row className="ha-replication-view__row">
          <Col xs={2} className="ha-replication-view__label">
            Shared Authentication Key
          </Col>
          <Col xs={10} className="ha-replication-view__value">
            {haConfig.cluster_key}
            <YBCopyButton text={haConfig.cluster_key} />
          </Col>
        </Row>
        {currentInstance.is_leader && (
          <>
            <Row className="ha-replication-view__row">
              <Col xs={2} className="ha-replication-view__label">
                Replication Frequency
              </Col>
              <Col xs={10} className="ha-replication-view__value">
                Every {schedule.frequency_milliseconds / FREQUENCY_MULTIPLIER} minute(s)
              </Col>
            </Row>
            <Row className="ha-replication-view__row">
              <Col xs={2} className="ha-replication-view__label">
                Enable Replication
              </Col>
              <Col xs={10} className="ha-replication-view__value">
                {schedule.is_running ? 'On' : 'Off'}
              </Col>
            </Row>
          </>
        )}
        <Row className="ha-replication-view__row">
          <Col xs={2} className="ha-replication-view__label">
            Cluster Topology
          </Col>
          <Col xs={10}>
            {sortedInstances.map((item) => (
              <div key={item.uuid} className="ha-replication-view__topology-row">
                <div className="ha-replication-view__topology-col ha-replication-view__topology-col--type">
                  <BadgeInstanceType isActive={item.is_leader} />
                </div>
                <div className="ha-replication-view__topology-col ha-replication-view__topology-col--current">
                  {item.is_local && <span className="badge badge-orange">Current</span>}
                </div>
                <div className="ha-replication-view__topology-col ha-replication-view__topology-col--address">
                  <a href={item.address} target="_blank" rel="noopener noreferrer">
                    {item.address}
                  </a>
                </div>
              </div>
            ))}
          </Col>
        </Row>
        {currentInstance.is_leader && (
          <Row className="ha-replication-view__row">
            <Col xs={2} className="ha-replication-view__label">
              Certificate Validation
            </Col>
            <Col xs={10} className="ha-replication-view__value">
              {haConfig.accept_any_certificate ? 'Disabled' : 'Enabled'}
            </Col>
          </Row>
        )}
      </Grid>
    );
  } else {
    return (
      <HAErrorPlaceholder
        error="Can't find an HA instance with is_local = true"
        configUUID={haConfig.uuid}
      />
    );
  }
};

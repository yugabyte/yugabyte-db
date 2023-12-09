import { FC, useEffect, useState } from 'react';
import _ from 'lodash';
import { Col, Grid, Row } from 'react-bootstrap';

import { YBButton } from '../../common/forms/fields';
import { HAConfig, HAReplicationSchedule } from '../../../redesign/helpers/dtos';
import { HAErrorPlaceholder } from '../compounds/HAErrorPlaceholder';
import { DeleteModal } from '../modals/DeleteModal';
import { PromoteInstanceModal } from '../modals/PromoteInstanceModal';
import { FREQUENCY_MULTIPLIER } from './HAReplicationForm';
import { BadgeInstanceType } from '../compounds/BadgeInstanceType';
import { YBCopyButton } from '../../common/descriptors';
import YBInfoTip from '../../common/descriptors/YBInfoTip';
import {
  ManagePeerCertsModal,
  PEER_CERT_PREFIX,
  PEER_CERT_SUFFIX
} from '../modals/ManagePeerCertsModal';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { isCertCAEnabledInRuntimeConfig } from '../../customCACerts';

import './HAReplicationView.scss';

interface DispatchProps {
  fetchRuntimeConfigs: () => void;
  setRuntimeConfig: (key: string, value: string) => void;
}

interface StateProps {
  runtimeConfigs: any;
}

interface OwnProps {
  config: HAConfig;
  schedule: HAReplicationSchedule;
  editConfig(): void;
}

type HAReplicationViewProps = StateProps & DispatchProps & OwnProps;

export interface PeerCert {
  data: string;
  type: string;
}

export interface YbHAWebService {
  ssl: {
    trustManager: {
      stores: PeerCert[];
    };
  };
}

export const YB_HA_WS_RUNTIME_CONFIG_KEY = 'yb.ha.ws';

const PEER_CERT_IDENTIFIER_LENGTH = 64;

export const EMPTY_YB_HA_WEBSERVICE = {
  ssl: {
    trustManager: {
      stores: []
    }
  }
};

export const getPeerCerts = (ybHAWebService: YbHAWebService) => {
  return ybHAWebService?.ssl?.trustManager?.stores;
};

/**
 * Returns a non-unique string identifier for a given peer certificate.
 * The identifier is the first 48 characters of data in the cert.
 * Whitespace is ignored.
 */
export const getPeerCertIdentifier = (peerCert: PeerCert) => {
  // PEM encoded certificates use base64 encoding.
  // We can ignore whitespace when selecting data to display as
  // an identifier.
  const compactCertData = peerCert.data.replace(/\s/g, '');
  const compactCertPrefix = PEER_CERT_PREFIX.replace(/\s/g, '');
  const compactCertSuffix = PEER_CERT_SUFFIX.replace(/\s/g, '');

  const identifierStartIndex = compactCertPrefix.length;
  const identifierEndIndex = Math.min(
    identifierStartIndex + PEER_CERT_IDENTIFIER_LENGTH,
    compactCertData.length - compactCertSuffix.length
  );

  return compactCertData.substring(identifierStartIndex, identifierEndIndex);
};

export const HAReplicationView: FC<HAReplicationViewProps> = ({
  config,
  schedule,
  runtimeConfigs,
  editConfig,
  fetchRuntimeConfigs,
  setRuntimeConfig
}) => {
  const [isDeleteModalVisible, setDeleteModalVisible] = useState(false);
  const [isPromoteModalVisible, setPromoteModalVisible] = useState(false);
  const [isAddPeerCertsModalVisible, setAddPeerCertsModalVisible] = useState(false);

  const showDeleteModal = () => setDeleteModalVisible(true);
  const hideDeleteModal = () => setDeleteModalVisible(false);
  const showPromoteModal = () => setPromoteModalVisible(true);
  const hidePromoteModal = () => setPromoteModalVisible(false);
  // fetch only specific key
  const showAddPeerCertModal = () => {
    fetchRuntimeConfigs();
    setAddPeerCertsModalVisible(true);
  };
  const hideAddPeerCertModal = () => {
    fetchRuntimeConfigs();
    setAddPeerCertsModalVisible(false);
  };

  const setYBHAWebserviceRuntimeConfig = (value: string) => {
    setRuntimeConfig(YB_HA_WS_RUNTIME_CONFIG_KEY, value);
  };

  useEffect(() => {
    fetchRuntimeConfigs();
  }, []); // eslint-disable-line react-hooks/exhaustive-deps

  const ybHAWebService: YbHAWebService =
    runtimeConfigs?.data && getPromiseState(runtimeConfigs).isSuccess()
      ? JSON.parse(
        runtimeConfigs.data.configEntries.find((c: any) => c.key === YB_HA_WS_RUNTIME_CONFIG_KEY)
          .value
      )
      : EMPTY_YB_HA_WEBSERVICE;

  const isCACertStoreEnabled = isCertCAEnabledInRuntimeConfig(runtimeConfigs?.data);
  // sort by is_leader to show active instance on the very top, then sort other items by address
  const sortedInstances = _.sortBy(config.instances, [(item) => !item.is_leader, 'address']);
  const currentInstance = sortedInstances.find((item) => item.is_local);
  if (currentInstance) {
    return (
      <Grid fluid className="ha-replication-view" data-testid="ha-replication-config-overview">
        <DeleteModal
          configId={config.uuid}
          isStandby={!currentInstance.is_leader}
          visible={isDeleteModalVisible}
          onClose={hideDeleteModal}
        />
        <PromoteInstanceModal
          configId={config.uuid}
          instanceId={currentInstance.uuid}
          visible={isPromoteModalVisible}
          onClose={hidePromoteModal}
        />
        {currentInstance.is_leader && (
          <ManagePeerCertsModal
            visible={isAddPeerCertsModalVisible}
            peerCerts={getPeerCerts(ybHAWebService)}
            setYBHAWebserviceRuntimeConfig={setYBHAWebserviceRuntimeConfig}
            onClose={hideAddPeerCertModal}
          />
        )}

        <Row>
          <Col xs={6}>
            <h4>Overview</h4>
          </Col>
          <Col xs={6} className="ha-replication-view__header-buttons">
            {currentInstance.is_leader ? (
              <>
                <YBButton btnText="Edit Configuration" onClick={editConfig} />
                {
                  !isCACertStoreEnabled && (
                    <YBButton
                      btnText={`${getPeerCerts(ybHAWebService).length > 0 ? 'Manage' : 'Add'
                        } Peer Certificates`}
                      onClick={(e: any) => {
                        showAddPeerCertModal();
                        e.currentTarget.blur();
                      }}
                    />
                  )
                }
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
              </>
            )}
            <YBButton btnText="Delete Configuration" onClick={showDeleteModal} />
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
            {config.cluster_key}
            <YBCopyButton text={config.cluster_key} />
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
        {!isCACertStoreEnabled && currentInstance.is_leader && (
          <Row className="ha-replication-view__row">
            <Col xs={2} className="ha-replication-view__label">
              Peer Certificates
            </Col>
            <Col xs={10}>
              {getPeerCerts(ybHAWebService).length === 0 ? (
                <button
                  className="ha-replication-view__no-cert--add-button"
                  onClick={showAddPeerCertModal}
                >
                  Add a peer certificate
                </button>
              ) : (
                getPeerCerts(ybHAWebService).map((peerCert) => {
                  return (
                    <>
                      <div className="ha-replication-view__cert-container">
                        <span className="ha-replication-view__cert-container--identifier">
                          {getPeerCertIdentifier(peerCert)}
                        </span>
                        <span className="ha-replication-view__cert-container--ellipse">
                          ( . . . )
                        </span>
                      </div>
                    </>
                  );
                })
              )}
            </Col>
          </Row>
        )}
      </Grid>
    );
  } else {
    return (
      <HAErrorPlaceholder
        error="Can't find an HA instance with is_local = true"
        configUUID={config.uuid}
      />
    );
  }
};

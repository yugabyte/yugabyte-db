import _ from 'lodash';
import React, { FC, useState } from 'react';
import { Col, Grid, Row } from 'react-bootstrap';
import { YBButton } from '../../common/forms/fields';
import { HAConfig, HAReplicationSchedule } from '../../../redesign/helpers/dtos';
import { HAReplicationError } from './HAReplicationError';
import { DeleteModal } from '../modals/DeleteModal';
import { PromoteInstanceModal } from '../modals/PromoteInstanceModal';
import { FREQUENCY_MULTIPLIER } from './HAReplicationForm';
import { BadgeInstanceType } from '../compounds/BadgeInstanceType';
import './HAReplicationView.scss';

interface HAReplicationViewProps {
  config: HAConfig;
  schedule: HAReplicationSchedule;
  editConfig(): void;
}

export const HAReplicationView: FC<HAReplicationViewProps> = ({ config, schedule, editConfig }) => {
  const [isDeleteModalVisible, setDeleteModalVisible] = useState(false);
  const [isPromoteModalVisible, setPromoteModalVisible] = useState(false);

  const showDeleteModal = () => setDeleteModalVisible(true);
  const hideDeleteModal = () => setDeleteModalVisible(false);
  const showPromoteModal = () => setPromoteModalVisible(true);
  const hidePromoteModal = () => setPromoteModalVisible(false);

  const sortedInstances = _.sortBy(config.instances, 'address');
  const currentInstance = sortedInstances.find((item) => item.is_local);
  if (currentInstance) {
    return (
      <Grid fluid className="ha-replication-view">
        <DeleteModal
          configId={config.uuid}
          visible={isDeleteModalVisible}
          onClose={hideDeleteModal}
        />
        <PromoteInstanceModal
          configId={config.uuid}
          instanceId={currentInstance.uuid}
          visible={isPromoteModalVisible}
          onClose={hidePromoteModal}
        />

        <Row>
          <Col xs={6}>
            <h4>Overview</h4>
          </Col>
          <Col xs={6} className="ha-replication-view__header-buttons">
            {currentInstance.is_leader && (
              <YBButton btnText="Edit Configuration" onClick={editConfig} />
            )}
            {!currentInstance.is_leader && (
              <YBButton
                btnText="Make Active"
                btnClass="btn btn-orange"
                btnIcon="fa fa-upload"
                onClick={showPromoteModal}
              />
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
      </Grid>
    );
  } else {
    return <HAReplicationError error="Can't find an HA instance with is_local = true" />;
  }
};

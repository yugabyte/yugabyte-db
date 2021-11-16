import React from 'react';
import { Col, ListGroup, ListGroupItem, Row } from 'react-bootstrap';
import { useQueries, useQuery } from 'react-query';
import { Link } from 'react-router';
import {
  fetchUniversesList,
  getUniverseInfo,
  getXclusterConfig
} from '../../actions/xClusterReplication';
import { YBLoading } from '../common/indicators';
import { IReplication } from './IClusterReplication';

import './ReplicationList.scss';
import {
  GetConfiguredThreshold,
  GetCurrentLag,
  getMasterNodeAddress,
  getReplicationStatus
} from './ReplicationUtils';

function ReplicationEmptyItem() {
  return <div className="replication-item replication-item-empty">No replications to show</div>;
}

function ReplicationItem({
  replication,
  currentUniverseUUID,
  universeName,
  masterNodeAddress
}: {
  replication: IReplication;
  currentUniverseUUID: string;
  universeName: string;
  masterNodeAddress: string;
}) {
  return (
    <div className="replication-item" key={replication.uuid}>
      <ListGroupItem>
        <Link to={`/universes/${currentUniverseUUID}/replication/${replication.uuid}`}>
          <Row>
            <Col lg={5} className="replication-name">
              {replication.name}
              {currentUniverseUUID !== replication.sourceUniverseUUID && (
                <span className="replication-target-universe">Target Universe</span>
              )}
            </Col>

            <Col lg={2} className="replication-date">
              <div className="replication-label">Started</div>
              <div className="replication-label-value">{replication.createTime}</div>
            </Col>
            <Col lg={2} className="replication-date">
              <div className="replication-label">Last Modified</div>
              <div>{replication.modifyTime}</div>
            </Col>
            <Col lg={1} lgPush={2} className="replication-status">
              {getReplicationStatus(replication.status)}
            </Col>
          </Row>
        </Link>
      </ListGroupItem>
      <Row className="replication-item-details">
        <Col lg={6}>
          <Row>
            <Col lg={12} className="noPadding">
              <span className="replication-label">Target Universe Name</span>
              <span className="replication-label-value">{universeName}</span>
              <br />
            </Col>
          </Row>
          <div className="replication-divider" />
          <Row>
            <Col lg={12} className="noPadding">
              <span className="replication-label">Master Node Address </span>
              <span className="replication-label-value">{masterNodeAddress}</span>
            </Col>
          </Row>
        </Col>
        <Col lg={6} className="replication-charts"></Col>
        <Col lg={6} className="replication-lag-details">
          <Row>
            <Col lg={12}>
              <Row style={{ display: 'flex', alignItems: 'center' }}>
                <Col lg={10} className="noPadding">
                  <span className="lag-text">Current lag</span>
                </Col>
                <Col lg={2} className="noPadding text-align-left">
                  <span className="lag">
                    <span className="lag-time">
                      <GetCurrentLag
                        replicationUUID={replication.uuid}
                        sourceUniverseUUID={replication.sourceUniverseUUID}
                      />
                    </span>
                    <span className="replication-label"> ms</span>
                  </span>
                </Col>
              </Row>
            </Col>
            <div className="replication-divider" />
            <Col lg={12}>
              <Row>
                <Col lg={10} className="noPadding">
                  <span className="lag-text">Max acceptable lag</span>
                </Col>
                <Col lg={2} className="noPadding text-align-left">
                  <span className="lag">
                    <span className="lag-value">
                      <GetConfiguredThreshold currentUniverseUUID={currentUniverseUUID} />
                    </span>
                    <span className="replication-label"> ms</span>
                  </span>
                </Col>
              </Row>
            </Col>
          </Row>
        </Col>
      </Row>
    </div>
  );
}

interface Props {
  currentUniverseUUID: string;
}

export function ReplicationList({ currentUniverseUUID }: Props) {
  const { data: universeInfo, isLoading: currentUniverseLoading } = useQuery(
    ['universe', currentUniverseUUID],
    () => getUniverseInfo(currentUniverseUUID)
  );

  const { sourceXClusterConfigs, targetXClusterConfigs } = universeInfo?.data?.universeDetails || {
    sourceXClusterConfigs: [],
    targetXClusterConfigs: []
  };

  const XclusterConfigList = Array.from(
    new Set([...sourceXClusterConfigs, ...targetXClusterConfigs])
  );

  const replicationData = useQueries(
    XclusterConfigList.map((uuid: string) => {
      return {
        queryKey: ['Xcluster', uuid],
        queryFn: () => getXclusterConfig(uuid),
        enabled: universeInfo?.data !== undefined
      };
    })
  );

  let masterNodeAddress = '';

  if (universeInfo?.data) {
    const {
      universeDetails: { nodeDetailsSet }
    } = universeInfo.data;
    masterNodeAddress = getMasterNodeAddress(nodeDetailsSet);
  }

  const { data: universeList, isLoading: isUniverseListLoading } = useQuery(['universeList'], () =>
    fetchUniversesList().then((res) => res.data)
  );

  if (currentUniverseLoading || isUniverseListLoading) {
    return <YBLoading />;
  }

  if (replicationData.length === 0) {
    return <ReplicationEmptyItem />;
  }

  const findTargetUniverseName = (universeUUID: string) =>
    universeList.find((universe: any) => universe.universeUUID === universeUUID)?.name;

  return (
    <ListGroup>
      {replicationData.map((replication: any) =>
        !replication.data ? (
          <YBLoading />
        ) : (
          <ReplicationItem
            key={replication.data.uuid}
            replication={replication.data}
            currentUniverseUUID={currentUniverseUUID}
            universeName={findTargetUniverseName(replication.data.targetUniverseUUID)}
            masterNodeAddress={masterNodeAddress}
          />
        )
      )}
    </ListGroup>
  );
}

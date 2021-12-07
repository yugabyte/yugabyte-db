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
import { GetConfiguredThreshold, GetCurrentLag, getReplicationStatus } from './ReplicationUtils';

function ReplicationEmptyItem() {
  return <div className="replication-item replication-item-empty">No replications to show</div>;
}

function ReplicationNameCard({
  isSource,
  clusterName,
  isCurrentCluster
}: {
  isSource: boolean;
  clusterName: string;
  isCurrentCluster: boolean;
}) {
  return (
    <div className={`replication-name-card ${isCurrentCluster ? 'active' : ''}`}>
      <div className="name-header">{isSource ? 'Source' : 'Target'}</div>
      <div className="cluster-name">{clusterName}</div>
    </div>
  );
}

function ReplicationItem({
  replication,
  currentUniverseUUID,
  targetUniverseName,
  sourceUniverseName
}: {
  replication: IReplication;
  currentUniverseUUID: string;
  targetUniverseName: string;
  sourceUniverseName: string;
}) {
  return (
    <div className="replication-item" key={replication.uuid}>
      <ListGroupItem>
        <Link to={`/universes/${currentUniverseUUID}/replication/${replication.uuid}`}>
          <Row>
            <Col lg={6} className="replication-name">
              {replication.name}
            </Col>
            <Col lg={6}>
              <Row className="replication-meta-details">
                <Col lg={4} className="replication-date">
                  <span className="replication-label">Started</span>
                  <span className="replication-label-value">{replication.createTime}</span>
                </Col>
                <Col lg={4} className="replication-date">
                  <span className="replication-label">Last modified</span>
                  <span>{replication.modifyTime}</span>
                </Col>
                <Col lg={4} lgPush={2} className="replication-status">
                  {getReplicationStatus(replication.status)}
                </Col>
              </Row>
            </Col>
          </Row>
        </Link>
      </ListGroupItem>
      <Row className="replication-item-details">
        <Col lg={6}>
          <Row className="replication-cluster-graph">
            <Col lg={5} className="noPaddingLeft">
              <ReplicationNameCard
                isSource={true}
                clusterName={sourceUniverseName}
                isCurrentCluster={currentUniverseUUID === replication.sourceUniverseUUID}
              />
            </Col>
            <Col lg={2} className="center-align-text">
              <i className="fa fa-long-arrow-right replication-name-arrow"></i>
            </Col>
            <Col lg={5}>
              <ReplicationNameCard
                isSource={false}
                clusterName={targetUniverseName}
                isCurrentCluster={currentUniverseUUID === replication.targetUniverseUUID}
              />
            </Col>
          </Row>
        </Col>
        <Col lg={6} className="replication-charts"></Col>
        <Col lg={6} className="replication-lag-details">
          <Row>
            <Col lg={12} className="noPaddingRight">
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
            <Col lg={12} className="noPaddingRight">
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
            targetUniverseName={findTargetUniverseName(replication.data.targetUniverseUUID)}
            sourceUniverseName={findTargetUniverseName(replication.data.sourceUniverseUUID)}
          />
        )
      )}
    </ListGroup>
  );
}

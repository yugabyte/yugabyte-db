import React from 'react';
import { useSelector } from 'react-redux';
import { Col, ListGroup, ListGroupItem, Row } from 'react-bootstrap';
import { useQueries, useQuery, useQueryClient } from 'react-query';
import { Link } from 'react-router';
import { useInterval } from 'react-use';
import _ from 'lodash';

import {
  fetchUniversesList,
  getUniverseInfo,
  getXclusterConfig
} from '../../actions/xClusterReplication';
import { YBLoading } from '../common/indicators';
import { Replication } from './XClusterReplicationTypes';

import {
  convertToLocalTime,
  findUniverseName,
  GetConfiguredThreshold,
  GetCurrentLag,
  getReplicationStatus
} from './ReplicationUtils';
import { TRANSITORY_STATES, XCLUSTER_CONFIG_REFETCH_INTERVAL_MS } from './constants';

import RightArrow from './ArrowIcon';

import './ReplicationList.scss';

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
  sourceUniverseName,
  currentUserTimezone
}: {
  replication: Replication;
  currentUniverseUUID: string;
  targetUniverseName: string;
  sourceUniverseName: string;
  currentUserTimezone: string;
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
                  <span className="replication-label-value">
                    {convertToLocalTime(replication.createTime, currentUserTimezone)}
                  </span>
                </Col>
                <Col lg={4} className="replication-date">
                  <span className="replication-label">Last modified</span>
                  <span>{convertToLocalTime(replication.modifyTime, currentUserTimezone)}</span>
                </Col>
                <Col lg={4} className="replication-status">
                  {getReplicationStatus(replication)}
                </Col>
              </Row>
            </Col>
          </Row>
        </Link>
      </ListGroupItem>
      <Row className="replication-item-details">
        <Col lg={6} md={12}>
          <Row className="replication-cluster-graph">
            <Col lg={5} className="noPaddingLeft">
              <ReplicationNameCard
                isSource={true}
                clusterName={sourceUniverseName}
                isCurrentCluster={currentUniverseUUID === replication.sourceUniverseUUID}
              />
            </Col>
            <Col lg={2} className="center-align-text">
              <span className="replication-name-arrow">
                <RightArrow />
              </span>
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
        <Col lg={6} md={12} className="replication-lag-details">
          <Row style={{ display: 'flex', justifyContent: 'flex-end' }}>
            <Col lg={3} className="lag noPadding">
              <div className="lag-text">Max acceptable lag</div>
              <div className="lag-time">
                <GetConfiguredThreshold currentUniverseUUID={currentUniverseUUID} />
              </div>
            </Col>
            <Col lg={3} className="lag noPadding">
              <div className="lag-text">Current Lag</div>
              <div className="lag-time">
                <GetCurrentLag
                  replicationUUID={replication.uuid}
                  sourceUniverseUUID={replication.sourceUniverseUUID}
                />
              </div>
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
  const currentUserTimezone = useSelector((state: any) => state.customer.currentUser.data.timezone);
  const queryClient = useQueryClient();

  const { data: universeInfo, isLoading: currentUniverseLoading } = useQuery(
    ['universe', currentUniverseUUID],
    () => getUniverseInfo(currentUniverseUUID)
  );
  const { data: universeList, isLoading: isUniverseListLoading } = useQuery(['universeList'], () =>
    fetchUniversesList().then((res) => res.data)
  );

  const sourceXClusterConfigUUIDs =
    universeInfo?.data?.universeDetails?.sourceXClusterConfigs ?? [];
  const targetXClusterConfigUUIDs =
    universeInfo?.data?.universeDetails?.targetXClusterConfigs ?? [];

  // List the XCluster Configurations for which the current universe is a source or a target.
  const universeXClusterConfigUUIDs = [...sourceXClusterConfigUUIDs, ...targetXClusterConfigUUIDs];

  const xClusterConfigs = useQueries(
    universeXClusterConfigUUIDs.map((uuid: string) => ({
      queryKey: ['Xcluster', uuid],
      queryFn: () => getXclusterConfig(uuid),
      enabled: universeInfo?.data !== undefined
    }))
  );

  useInterval(() => {
    xClusterConfigs.forEach((xClusterConfig: any) => {
      if (
       xClusterConfig?.data?.status &&
        _.includes(TRANSITORY_STATES, xClusterConfig.data.status)
      ) {
        queryClient.invalidateQueries('Xcluster');
      }
    });
  }, XCLUSTER_CONFIG_REFETCH_INTERVAL_MS);

  if (currentUniverseLoading || isUniverseListLoading) {
    return <YBLoading />;
  }

  if (xClusterConfigs.length === 0) {
    return <ReplicationEmptyItem />;
  }

  return (
    <ListGroup>
      {xClusterConfigs.map((xClusterConfig: any) =>
        !xClusterConfig.data ? (
          <YBLoading />
        ) : (
          <ReplicationItem
            key={xClusterConfig.data.uuid}
            replication={xClusterConfig.data}
            currentUniverseUUID={currentUniverseUUID}
            targetUniverseName={findUniverseName(
              universeList,
              xClusterConfig.data.targetUniverseUUID
            )}
            sourceUniverseName={findUniverseName(
              universeList,
              xClusterConfig.data.sourceUniverseUUID
            )}
            currentUserTimezone={currentUserTimezone}
          />
        )
      )}
    </ListGroup>
  );
}

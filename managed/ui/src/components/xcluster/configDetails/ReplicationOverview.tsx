import React from 'react';
import { Col, Row } from 'react-bootstrap';
import { useSelector } from 'react-redux';
import { Link } from 'react-router';
import { XClusterConfig } from '../XClusterTypes';
import { convertToLocalTime, getMasterNodeAddress } from '../ReplicationUtils';

export function ReplicationOverview({
  replication,
  destinationUniverse
}: {
  replication: XClusterConfig;
  destinationUniverse: any;
}) {
  const {
    universeDetails: { nodeDetailsSet }
  } = destinationUniverse;
  const currentUserTimezone = useSelector((state: any) => state.customer.currentUser.data.timezone);

  return (
    <>
      <Row className="replication-overview">
        <Col lg={12}>
          <Row>
            <Col lg={2} className="noLeftPadding replication-label">
              Replication started
            </Col>
            <Col lg={2}>{convertToLocalTime(replication.createTime, currentUserTimezone)}</Col>
          </Row>
          <div className="replication-divider" />
          <Row>
            <Col lg={2} className="noLeftPadding replication-label">
              Replication last modified
            </Col>
            <Col lg={2}>{convertToLocalTime(replication.modifyTime, currentUserTimezone)}</Col>
          </Row>
        </Col>
      </Row>
      <div className="replication-divider" />
      <Row style={{ paddingLeft: '20px' }}>
        <Col lg={12}>
          <b>Replication's Target Universe</b>
        </Col>
      </Row>
      <div className="replication-divider" />
      <Row className="replication-target-universe">
        <Col lg={12} className="noLeftPadding">
          <Row>
            <Col lg={2} className="replication-label">
              Name
            </Col>
            <Col lg={3}>
              <Link
                to={`/universes/${destinationUniverse.universeUUID}`}
                className="target-universe-link"
              >
                {destinationUniverse.name}
              </Link>
              <span className="target-universe-subText">Target</span>
            </Col>
          </Row>
          <div className="replication-divider" />
          <Row>
            <Col lg={2} className="replication-label">
              UUID
            </Col>
            <Col lg={3}>{replication.targetUniverseUUID}</Col>
          </Row>
          <div className="replication-divider" />
          <Row>
            <Col lg={2} className="replication-label">
              Master node address
            </Col>
            <Col lg={3}>{getMasterNodeAddress(nodeDetailsSet)}</Col>
          </Row>
          <div className="replication-divider" />
          <Row>
            <Col lg={2} className="replication-label">
              Provider
            </Col>
            <Col lg={3}>{nodeDetailsSet[0].cloudInfo.cloud}</Col>
          </Row>
          <div className="replication-divider" />
          <Row>
            <Col lg={2} className="replication-label">
              Region
            </Col>
            <Col lg={3}>{nodeDetailsSet[0].cloudInfo.region}</Col>
          </Row>
          <div className="replication-divider" />
        </Col>
      </Row>
    </>
  );
}

// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Grid, Row, Col } from 'react-bootstrap';
import RegionMap from './RegionMap';
import NodeDetails from './NodeDetails';
import UniverseInfoPanel from './UniverseInfoPanel';

export default class UniverseDetail extends Component {
  static contextTypes = {
    router: PropTypes.object
  }

  componentWillUnmount() {
    this.props.resetUniverseInfo();
  }

  componentDidMount() {
    this.props.getUniverseInfo(this.props.uuid);
  }

  render() {
    const { currentUniverse, loading } = this.props.universe;
    if (loading) {
      return <div className="container">Loading...</div>;
    } else if (!currentUniverse) {
      return <span />;
    }

    return (
      <Grid id="page-wrapper">
        <Row className="header-row">
          <h3>Universe { currentUniverse.name }</h3>
        </Row>
        <Row>
          <Col md={6}>
            <RegionMap regions={currentUniverse.regions}/>
          </Col>
          <Col md={6}>
            <UniverseInfoPanel universeInfo={currentUniverse} />
          </Col>
        </Row>
        <Row>
          <NodeDetails nodeDetails={currentUniverse.universeDetails.nodeDetailsMap}/>
        </Row>
      </Grid>);
  }
}

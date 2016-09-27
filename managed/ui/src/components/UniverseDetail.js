// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Grid, Row, Col, ButtonGroup } from 'react-bootstrap';
import RegionMap from './RegionMap';
import NodeDetails from './NodeDetails';
import UniverseInfoPanel from './UniverseInfoPanel';
import ConnectStringPanel from './ConnectStringPanel';
import GraphPanelContainer from '../containers/GraphPanelContainer';
import UniverseModalContainer from '../containers/UniverseModalContainer';
import DeleteUniverseContainer from '../containers/DeleteUniverseContainer';
import TaskProgressContainer from '../containers/TaskProgressContainer';

export default class UniverseDetail extends Component {
  static contextTypes = {
    router: PropTypes.object
  }

  componentWillUnmount() {
    this.props.resetUniverseInfo();
    this.props.resetUniverseTasks();
  }

  componentDidMount() {
    var uuid ;
    if (typeof this.props.universeSelectionId !== "undefined") {
      uuid = this.props.universeUUID;
    } else {
      uuid = this.props.uuid;
    }
    this.props.getUniverseInfo(uuid);
    this.props.fetchUniverseTasks(uuid);
  }

  render() {
    const { universe: { currentUniverse, universeTasks, loading } } = this.props;
    if (loading) {
      return <div className="container">Loading...</div>;
    } else if (!currentUniverse) {
      return <span />;
    }
    const { universeDetails } = currentUniverse;
    var universeTaskUUIDs = []
    if (universeTasks && universeTasks[currentUniverse.universeUUID] !== undefined) {
      universeTaskUUIDs = universeTasks[currentUniverse.universeUUID].map(function(task) {
        return (task.percentComplete !== 100) ? task.id : false;
      }).filter(Boolean);
    }

    return (
      <Grid id="page-wrapper">
        <Row className="header-row">
          <Col lg={9} >
            <h3>Universe { currentUniverse.name }</h3>
          </Col>
          <ButtonGroup>
            <UniverseModalContainer type="Edit" />
            <DeleteUniverseContainer uuid={currentUniverse.universeUUID} />
          </ButtonGroup>
        </Row>
        <Row>
          <Col lg={11}>
            <ConnectStringPanel universeId={currentUniverse.universeUUID}
                                customerId={localStorage.getItem("customer_id")} />
          </Col>
        </Row>
        <Row>
          <Col md={6}>
            <RegionMap regions={currentUniverse.regions}/>
          </Col>
          <Col md={5} lg={5}>
            <UniverseInfoPanel universeInfo={currentUniverse} />
            <TaskProgressContainer taskUUIDs={universeTaskUUIDs} />
          </Col>
        </Row>
        <Row>
          <Col lg={11}>
            <NodeDetails nodeDetails={universeDetails.nodeDetailsMap}/>
          </Col>
        </Row>
        <Row>
          <Col lg={11}>
            <GraphPanelContainer nodePrefix={universeDetails.nodePrefix} />
          </Col>
        </Row>
      </Grid>
    );
  }
}

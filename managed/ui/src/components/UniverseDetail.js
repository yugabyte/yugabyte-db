// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Grid, Row, Col, ButtonGroup, Image,
          DropdownButton, MenuItem } from 'react-bootstrap';
import RegionMap from './RegionMap';
import NodeDetails from './NodeDetails';
import UniverseInfoPanel from './UniverseInfoPanel';
import ConnectStringPanel from './ConnectStringPanel';
import GraphPanelContainer from '../containers/GraphPanelContainer';
import TaskProgressContainer from '../containers/TaskProgressContainer';
import YBButton from './fields/YBButton';
import UniverseFormContainer from '../containers/forms/UniverseFormContainer';
import GFlagsFormContainer from '../containers/forms/GFlagsFormContainer';
import {Link} from 'react-router';
import universelogo from '../images/universe_icon.png';
import YBLabelWithIcon from './fields/YBLabelWithIcon';

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
    const { universe: { currentUniverse, universeTasks, loading, showModal, visibleModal } } = this.props;
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
          <Col lg={9}>
            <div className="detail-label-small">
               <Link to="/universes">
                 <YBLabelWithIcon icon="fa fa-chevron-left fa-fw">
                   Universes
                 </YBLabelWithIcon>
               </Link>
            </div>
            <div>
              <h2><Image src={universelogo}/> Universe { currentUniverse.name }</h2>
            </div>
          </Col>
          <Col lg={3}>
            <div className="detail-label-small">
              Test And Verify
            </div>
            <ButtonGroup className="universe-detail-btn-group">
              <YBButton btnClass=" btn btn-default btn-sm bg-orange"
                        btnText="Edit" btnIcon="fa fa-database" onClick={this.props.showUniverseModal} />
              <DropdownButton bsStyle="btn btn-default btn-sm " title="More" id="bg-nested-dropdown" >
                <MenuItem eventKey="1">
                  <YBLabelWithIcon icon="fa fa-refresh fa-fw">
                    s/w Upgrades
                  </YBLabelWithIcon>
                </MenuItem>
                <MenuItem eventKey="2" onClick={this.props.showGFlagsModal} >
                  <YBLabelWithIcon icon="fa fa-flag fa-fw">
                    Flags
                  </YBLabelWithIcon>
                </MenuItem>
              </DropdownButton>
              <YBButton btnIcon="fa fa-trash-o" btnClass="btn btn-default btn-sm" />
            </ButtonGroup>
          </Col>
          <UniverseFormContainer type="Edit"
                                 visible={showModal===true && visibleModal==="universeModal"}
                                 onHide={this.props.closeModal} title="Edit Universe" />
          <GFlagsFormContainer modalVisible={showModal === true && visibleModal === "gFlagsModal"}
                               onHide={this.props.closeModal} title="Set GFlags"/>

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
            <NodeDetails nodeDetails={universeDetails.nodeDetailsSet}/>
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

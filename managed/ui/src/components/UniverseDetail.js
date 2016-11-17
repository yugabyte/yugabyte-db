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
import DeleteUniverseContainer from '../containers/DeleteUniverseContainer';
import YBButton from './fields/YBButton';
import UniverseFormContainer from '../containers/forms/UniverseFormContainer';
import GFlagsFormContainer from '../containers/forms/GFlagsFormContainer';
import {Link} from 'react-router';
import universelogo from '../images/universe_icon.png';
import YBLabelWithIcon from './fields/YBLabelWithIcon';
import YBPanelItem from './YBPanelItem';
import YBMapLegendItem from './YBMapLegendItem';

class YBMapLegend extends Component {
  render() {
    const {regions} = this.props;
    var rootRegions = regions;
    var asyncRegions = [{"name": "No Async Replicas Added."}];
    var cacheRegions = [{"name": "No Caches Added."}];
    return (
      <div className="map-legend-container">
        <YBMapLegendItem regions={rootRegions} title={"Root Data"} type="Root"/>
        <YBMapLegendItem regions={asyncRegions} title="Async Data Replication" type="Async"/>
        <YBMapLegendItem regions={cacheRegions} title="Cache Data" type="Cache"/>
      </div>
    )
  }
}

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
    var universeTaskUUIDs = [];
    if (universeTasks && universeTasks[currentUniverse.universeUUID] !== undefined) {
      universeTaskUUIDs = universeTasks[currentUniverse.universeUUID].map(function(task) {
        return (task.percentComplete !== 100) ? task.id : false;
      }).filter(Boolean);
    }

    return (
      <Grid id="page-wrapper" fluid={true}>
        <Row className="header-row">
          <Col lg={10}>
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
          <Col lg={2}>
            <div className="detail-label-small">
              Test And Verify
            </div>
            <ButtonGroup className="universe-detail-btn-group">
              <YBButton btnClass=" btn btn-default btn-sm bg-orange"
                        btnText="Edit" btnIcon="fa fa-database" onClick={this.props.showUniverseModal} />
              <DropdownButton className="btn btn-default btn-sm " title="More" id="bg-nested-dropdown" >
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
              <YBButton btnIcon="fa fa-trash-o" btnClass="btn btn-default btn-sm" onClick={this.props.showDeleteUniverseModal}/>
            </ButtonGroup>
          </Col>
          <UniverseFormContainer type="Edit"
                                 visible={showModal===true && visibleModal==="universeModal"}
                                 onHide={this.props.closeModal} title="Edit Universe" />
          <GFlagsFormContainer modalVisible={showModal === true && visibleModal === "gFlagsModal"}
                               onHide={this.props.closeModal} title="Set GFlags"/>
          <DeleteUniverseContainer visible={showModal===true && visibleModal==="deleteUniverseModal"}
                                       onHide={this.props.closeModal} title="Delete Universe"/>
        </Row>
        <Row>
          <Col lg={12}>
            <ConnectStringPanel universeId={currentUniverse.universeUUID}
                                customerId={localStorage.getItem("customer_id")} />
          </Col>
        </Row>
        <Row>
          <Col md={12}>
            <YBPanelItem name={"Region Placement"}>
            <Col lg={4}>
              <YBMapLegend regions={currentUniverse.regions}/>
            </Col>
            <Col lg={8}>
            <RegionMap regions={currentUniverse.regions} type={"Root"} />
            </Col>
            </YBPanelItem>
          </Col>
          <Col md={12} lg={12}>
            <UniverseInfoPanel universeInfo={currentUniverse} />
            <TaskProgressContainer taskUUIDs={universeTaskUUIDs} />
          </Col>
        </Row>
        <Row>
          <Col lg={12}>
            <NodeDetails nodeDetails={universeDetails.nodeDetailsSet}/>
          </Col>
        </Row>
        <Row>
          <Col lg={12}>
            <GraphPanelContainer nodePrefix={universeDetails.nodePrefix} />
          </Col>
        </Row>
      </Grid>
    );
  }
}

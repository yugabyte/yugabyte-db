// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Grid, Row, Col, ButtonGroup, Image,
          DropdownButton, MenuItem, Tab } from 'react-bootstrap';
import RegionMap from './maps/RegionMap';
import UniverseInfoPanel from './UniverseInfoPanel';
import ConnectStringPanel from './ConnectStringPanel';
import GraphPanelContainer from '../containers/GraphPanelContainer';
import TaskProgressContainer from '../containers/TaskProgressContainer';
import RollingUpgradeFormContainer from '../containers/forms/RollingUpgradeFormContainer';
import DeleteUniverseContainer from '../containers/DeleteUniverseContainer';
import YBButton from './fields/YBButton';
import UniverseFormContainer from '../containers/forms/UniverseFormContainer';
import {Link} from 'react-router';
import universelogo from '../images/universe_icon.png';
import YBLabelWithIcon from './fields/YBLabelWithIcon';
import YBPanelItem from './YBPanelItem';
import YBMapLegendItem from './maps/YBMapLegendItem';
import NodeDetails from './NodeDetails';
import YBTabsPanel from './panels/YBTabsPanel';


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

              <DropdownButton className="btn btn-default btn-sm" title="More" id="bg-nested-dropdown" >
                <MenuItem eventKey="1" onClick={this.props.showSoftwareUpgradesModal}>

                  <YBLabelWithIcon icon="fa fa-refresh fa-fw">
                    s/w Upgrades
                  </YBLabelWithIcon>
                </MenuItem>
                <MenuItem eventKey="2" onClick={this.props.showGFlagsModal} >
                  <YBLabelWithIcon icon="fa fa-flag fa-fw">
                    GFlags
                  </YBLabelWithIcon>
                </MenuItem>
              </DropdownButton>
              <YBButton btnIcon="fa fa-trash-o" btnClass="btn btn-default btn-sm" onClick={this.props.showDeleteUniverseModal}/>
            </ButtonGroup>
          </Col>
          <UniverseFormContainer type="Edit"
                                 visible={showModal===true && visibleModal==="universeModal"}
                                 onHide={this.props.closeModal} title="Edit Universe" />

          <RollingUpgradeFormContainer modalVisible={showModal === true &&
          (visibleModal === "gFlagsModal" || visibleModal ==="softwareUpgradesModal")}
                               onHide={this.props.closeModal} />


          <DeleteUniverseContainer visible={showModal===true && visibleModal==="deleteUniverseModal"}
                                       onHide={this.props.closeModal} title="Delete Universe"/>

        </Row>
        <YBTabsPanel activeTab={"overview"} id={"universe-tab-panel"}>
          <Tab eventKey={"overview"} title="Overview">
            <ConnectStringPanel universeId={currentUniverse.universeUUID}
                                customerId={localStorage.getItem("customer_id")} />
          </Tab>
          <Tab eventKey={"config"} title="Config">
            <UniverseInfoPanel universeInfo={currentUniverse} />
            <TaskProgressContainer taskUUIDs={universeTaskUUIDs} />
          </Tab>
          <Tab eventKey={"placement"} title="Placement">
            <YBPanelItem name={"Region Placement"}>
            <Col lg={4}>
              <YBMapLegend regions={currentUniverse.regions}/>
            </Col>
            <Col lg={8}>
            <RegionMap regions={currentUniverse.regions} type={"Root"} />
            </Col>
            </YBPanelItem>
          </Tab>
          <Tab eventKey={"nodes"} title="Nodes">
            <NodeDetails nodeDetails={currentUniverse.universeDetails.nodeDetailsSet}/>
          </Tab>
          <Tab eventKey={"metrics"} title="Metrics">
            <GraphPanelContainer nodePrefix={currentUniverse.universeDetails.nodePrefix} origin={"universe"} universeUUID={currentUniverse.universeUUID} />
          </Tab>
        </YBTabsPanel>
      </Grid>
    );
  }
}

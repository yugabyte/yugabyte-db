// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Link } from 'react-router';
import { Grid, Row, Col, ButtonGroup, Image,
  DropdownButton, MenuItem, Tab } from 'react-bootstrap';
import { UniverseInfoPanel, ConnectStringPanel } from '../panels'
import { GraphPanelContainer, GraphPanelHeaderContainer } from '../../containers/metrics';
import { TaskProgressContainer } from '../../containers/tasks';
import { RollingUpgradeFormContainer,
  UniverseFormContainer } from '../../containers/common/forms';
import { DeleteUniverseContainer } from '../../containers/universes';
import { YBButton } from '../common/forms/fields';
import { YBLabelWithIcon } from '../common/descriptors';
import { YBTabsPanel, YBPanelItem } from '../panels';
import { YBMapLegendItem, RegionMap } from '../maps';
import { NodeDetails } from '.';
import universelogo from './images/universe_icon.png';

class YBMapLegend extends Component {
  render() {
    const {regions} = this.props;
    var rootRegions = regions;
    var asyncRegions = [{"name": "No Async Replicas Added."}];
    var cacheRegions = [{"name": "No Caches Added."}];
    return (
      <div className="map-legend-container">
        <YBMapLegendItem regions={rootRegions} title={"Root Data"} type="Root"/>
        <YBMapLegendItem regions={asyncRegions} title={"Async Replica"} type="Async"/>
        <YBMapLegendItem regions={cacheRegions} title={"Remote Cache"} type="Cache"/>
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

    var tabElements = [
      <Tab eventKey={"overview"} title="Overview" key="overview-tab">
        <Col lg={4} style={{padding: 0}}>
          <UniverseInfoPanel universeInfo={currentUniverse} />
        </Col>
        <Col lg={8}>
          <ConnectStringPanel universeId={currentUniverse.universeUUID}
                              customerId={localStorage.getItem("customer_id")}
                              universeInfo={currentUniverse} />
        </Col>
        <YBPanelItem name={"Placement Policy"}>
          <Col lg={4}>
            <YBMapLegend regions={currentUniverse.regions}/>
          </Col>
          <Col lg={8}>
            <RegionMap regions={currentUniverse.regions} type={"Root"} />
          </Col>
        </YBPanelItem>
      </Tab>,
      <Tab eventKey={"nodes"} title="Nodes" key="nodes-tab">
        <NodeDetails nodeDetails={currentUniverse.universeDetails.nodeDetailsSet}/>
      </Tab>,
      <Tab eventKey={"metrics"} title="Metrics" key="metrics-tab">
        <GraphPanelHeaderContainer>
          <GraphPanelContainer
            type={"server"}
            nodePrefixes={[currentUniverse.universeDetails.nodePrefix]} />
        </GraphPanelHeaderContainer>
      </Tab>]

    if (universeTaskUUIDs.length > 0) {
      tabElements.push(
         <Tab eventKey={"tasks"} title="Tasks" key="tasks-tab">
           <TaskProgressContainer taskUUIDs={universeTaskUUIDs} type="StepBar"/>
         </Tab>)
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
          { tabElements }
        </YBTabsPanel>
      </Grid>
    );
  }
}

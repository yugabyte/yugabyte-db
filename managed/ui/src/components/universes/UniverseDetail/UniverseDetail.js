// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Link } from 'react-router';
import { Grid, Row, Col, ButtonGroup, DropdownButton, MenuItem, Tab } from 'react-bootstrap';
import { UniverseInfoPanel, ConnectStringPanel } from '../../panels'
import { GraphPanelContainer, GraphPanelHeaderContainer } from '../../metrics';
import { TaskProgressContainer } from '../../tasks';
import { RollingUpgradeFormContainer } from 'components/common/forms';
import { UniverseFormContainer, UniverseStatus, NodeDetails, DeleteUniverseContainer } from '..';
import { YBButton } from '../../common/forms/fields';
import { YBLabelWithIcon } from '../../common/descriptors';
import { YBTabsPanel } from '../../panels';
import { RegionMap } from '../../maps';
import { ListTablesContainer } from '../../tables';
import { YBMapLegend } from '../../maps';

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
        <Row>
          <Col lg={4}>
            <UniverseInfoPanel universeInfo={currentUniverse}
                               customerId={localStorage.getItem("customer_id")} />
          </Col>
          <Col lg={8}>
            <ConnectStringPanel customerId={localStorage.getItem("customer_id")}
                                universeInfo={currentUniverse} />
          </Col>
        </Row>
        <Row>
          <Col lg={12}>
            <RegionMap regions={currentUniverse.regions} type={"Root"} />
            <YBMapLegend title="Placement Policy" regions={currentUniverse.regions}/>
          </Col>
        </Row>
      </Tab>,
      <Tab eventKey={"tables"} title="Tables" key="tables-tab">
        <ListTablesContainer/>
      </Tab>,
      <Tab eventKey={"nodes"} title="Nodes" key="nodes-tab">
        <NodeDetails nodeDetails={currentUniverse.universeDetails.nodeDetailsSet}/>
      </Tab>,
      <Tab eventKey={"metrics"} title="Metrics" key="metrics-tab">
        <GraphPanelHeaderContainer origin={"universe"}>
          <GraphPanelContainer
            type={"cql"}
            nodePrefixes={[currentUniverse.universeDetails.nodePrefix]} />
          <GraphPanelContainer
            type={"redis"}
            nodePrefixes={[currentUniverse.universeDetails.nodePrefix]} />
          <GraphPanelContainer
            type={"tserver"}
            nodePrefixes={[currentUniverse.universeDetails.nodePrefix]} />
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
              <h2>
                { currentUniverse.name }
                <UniverseStatus universe={currentUniverse} showLabelText={true} />
              </h2>
            </div>
          </Col>
          <Col lg={2} className="page-action-buttons">
            <ButtonGroup className="universe-detail-btn-group">
              <YBButton btnClass=" btn btn-default bg-orange"
                        btnText="Edit" btnIcon="fa fa-database" onClick={this.props.showUniverseModal} />

              <DropdownButton className="btn btn-default" title="More" id="bg-nested-dropdown" pullRight>
                <MenuItem eventKey="1" onClick={this.props.showSoftwareUpgradesModal}>

                  <YBLabelWithIcon icon="fa fa-refresh fa-fw">
                    Upgrade Software
                  </YBLabelWithIcon>
                </MenuItem>
                <MenuItem eventKey="2" onClick={this.props.showGFlagsModal} >
                  <YBLabelWithIcon icon="fa fa-flag fa-fw">
                    Show GFlags
                  </YBLabelWithIcon>
                </MenuItem>
                <MenuItem eventKey="2" onClick={this.props.showDeleteUniverseModal} >
                  <YBLabelWithIcon icon="fa fa-trash-o fa-fw">
                    Delete Universe
                  </YBLabelWithIcon>
                </MenuItem>
              </DropdownButton>
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

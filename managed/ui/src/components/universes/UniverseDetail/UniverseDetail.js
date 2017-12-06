// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Link, withRouter, browserHistory} from 'react-router';
import { Grid, Row, Col, DropdownButton, MenuItem, Tab } from 'react-bootstrap';
import Measure from 'react-measure';
import { UniverseInfoPanel, ResourceStringPanel } from '../../panels';
import { CustomerMetricsPanel } from '../../metrics';
import { TaskProgressContainer, TaskListTable } from '../../tasks';
import { RollingUpgradeFormContainer } from 'components/common/forms';
import { UniverseFormContainer, UniverseStatusContainer, NodeDetails,
         DeleteUniverseContainer, UniverseAppsModal } from '../../universes';
import { UniverseResources } from '../UniverseResources';
import { YBButton } from '../../common/forms/fields';
import { YBLabelWithIcon } from '../../common/descriptors';
import { YBTabsPanel, YBPanelItem } from '../../panels';
import { RegionMap } from '../../maps';
import { ListTablesContainer } from '../../tables';
import { YBMapLegend } from '../../maps';
import { isEmptyObject, isNonEmptyObject, isNonEmptyArray } from 'utils/ObjectUtils';
import { getPromiseState } from 'utils/PromiseUtils';
import { YBLoading } from '../../common/indicators';
import { mouseTrap } from 'react-mousetrap';
import './UniverseDetail.scss';

class UniverseDetail extends Component {
  constructor(props) {
    super(props);
    this.onEditUniverseButtonClick = this.onEditUniverseButtonClick.bind(this);
    this.getUniverseInfo = this.getUniverseInfo.bind(this);
    this.state = {
      dimensions: {},
    };
  }

  componentWillUnmount() {
    this.props.resetUniverseInfo();
    this.props.resetUniverseTasks();
    this.props.resetMasterLeader();
  }

  componentWillMount() {
    this.props.bindShortcut(['ctrl+e', 'e'], this.onEditUniverseButtonClick);

    if (this.props.location.pathname !== "/universes/create") {
      let uuid = this.props.uuid;
      if (typeof this.props.universeSelectionId !== "undefined") {
        uuid = this.props.universeUUID;
      }
      this.props.getUniverseInfo(uuid);
      this.props.fetchUniverseTasks(uuid);
    }
  }

  onResize(dimensions) {
    dimensions.width -= 40;
    this.setState({dimensions});
  }

  onEditUniverseButtonClick() {
    const location = Object.assign({}, browserHistory.getCurrentLocation());
    const query = {edit: true};
    Object.assign(location.query, query);
    browserHistory.push(location);
  }

  getUniverseInfo() {
    const universeUUID = this.props.universe.currentUniverse.data.universeUUID;
    this.props.getUniverseInfo(universeUUID);
  }

  render() {
    const { universe: { currentUniverse, showModal, visibleModal }, universe, location: {query}} = this.props;
    const placementInfoRegionList = isNonEmptyObject(currentUniverse.data) ? currentUniverse.data.universeDetails.placementInfo.cloudList[0].regionList : [];
    if (this.props.location.pathname === "/universes/create") {
      return <UniverseFormContainer type="Create"/>;
    }
    if (getPromiseState(currentUniverse).isLoading() || getPromiseState(currentUniverse).isInit()) {
      return <YBLoading />;
    } else if (isEmptyObject(currentUniverse.data)) {
      return <span />;
    }
    if (isNonEmptyObject(query) && query.edit) {
      return <UniverseFormContainer type="Edit" />;
    }
    const width = this.state.dimensions.width;
    const nodePrefixes = [currentUniverse.data.universeDetails.nodePrefix];
    const tabElements = [
      <Tab eventKey={"overview"} title="Overview" key="overview-tab" mountOnEnter={true} unmountOnExit={true}>
        <YBPanelItem
          body={
            <div>
              <div className="universe-detail-flex-container">
                <UniverseResources resources={currentUniverse.data.resources} renderType={"Display"}/>
              </div>
              <Row>
                <Col lg={5}>
                  <UniverseInfoPanel universeInfo={currentUniverse.data}
                                    customerId={localStorage.getItem("customer_id")} />
                </Col>
                <Col lg={7}>
                  <ResourceStringPanel customerId={localStorage.getItem("customer_id")}
                                      universeInfo={currentUniverse.data} />
                </Col>
              </Row>
              <Row>
                <Col lg={12}>
                  <RegionMap universe={currentUniverse.data} type={"Universe"} />
                  <YBMapLegend title="Data Placement (In AZs)" regions={placementInfoRegionList} type="Universe"/>
                </Col>
              </Row>
            </div>
          }
        />
      </Tab>,
      <Tab eventKey={"tables"} title="Tables" key="tables-tab"mountOnEnter={true} unmountOnExit={true}>
        <ListTablesContainer/>
      </Tab>,
      <Tab eventKey={"nodes"} title="Nodes" key="nodes-tab" mountOnEnter={true} unmountOnExit={true}>
        <NodeDetails {...this.props}/>
      </Tab>,
      <Tab eventKey={"metrics"} title="Metrics" key="metrics-tab" mountOnEnter={true} unmountOnExit={true}>
        <div className="universe-detail-content-container">
          <CustomerMetricsPanel origin={"universe"} width={width} nodePrefixes={nodePrefixes} />
        </div>
      </Tab>,
      <Tab eventKey={"tasks"} title="Tasks" key="tasks-tab" mountOnEnter={true} unmountOnExit={true}>
        <UniverseTaskList universe={universe}/>
      </Tab>
    ];
    const currentBreadCrumb = (
      <div className="detail-label-small">
        <Link to="/universes">
          <YBLabelWithIcon>
            Universes
          </YBLabelWithIcon>
        </Link>
        <Link to={`/universes/${currentUniverse.data.universeUUID}`}>
          <YBLabelWithIcon icon="fa fa-angle-right fa-fw">
            {currentUniverse.data.name}
          </YBLabelWithIcon>
        </Link>
      </div>
    );

    return (
      <Grid id="page-wrapper" fluid={true} className="universe-details">
        <Row>
          <Col lg={10} sm={8} xs={6}>
            {/* UNIVERSE NAME */}

            {currentBreadCrumb}
            <div className="universe-detail-status-container">
              <h2>
                { currentUniverse.data.name }
              </h2>
              <UniverseStatusContainer currentUniverse={currentUniverse.data} showLabelText={true} refreshUniverseData={this.getUniverseInfo}/>
            </div>


          </Col>
          <Col lg={2} sm={4}  xs={6} className="page-action-buttons">


            {/* UNIVERSE EDIT */}
            <div className="universe-detail-btn-group">
              <UniverseAppsModal nodeDetails={currentUniverse.data.universeDetails.nodeDetailsSet}/>
              <YBButton btnClass=" btn btn-orange"
                        btnText="Edit" btnIcon="fa fa-database" onClick={this.onEditUniverseButtonClick} />

              <DropdownButton title="More" id="bg-nested-dropdown" pullRight>
                <MenuItem eventKey="1" onClick={this.props.showSoftwareUpgradesModal}>

                  <YBLabelWithIcon icon="fa fa-refresh fa-fw">
                    Upgrade Software
                  </YBLabelWithIcon>
                </MenuItem>
                <MenuItem eventKey="2" onClick={this.props.showGFlagsModal} >
                  <YBLabelWithIcon icon="fa fa-flag fa-fw">
                    Edit GFlags
                  </YBLabelWithIcon>
                </MenuItem>
                <MenuItem eventKey="2" onClick={this.props.showDeleteUniverseModal} >
                  <YBLabelWithIcon icon="fa fa-trash-o fa-fw">
                    Delete Universe
                  </YBLabelWithIcon>
                </MenuItem>
              </DropdownButton>
            </div>
          </Col>
          <RollingUpgradeFormContainer modalVisible={showModal &&
          (visibleModal === "gFlagsModal" || visibleModal ==="softwareUpgradesModal")}
                                       onHide={this.props.closeModal} />
          <DeleteUniverseContainer visible={showModal && visibleModal==="deleteUniverseModal"}
                                   onHide={this.props.closeModal} title="Delete Universe"/>
        </Row>

        <Measure onMeasure={this.onResize.bind(this)}>
          <YBTabsPanel defaultTab={"overview"} id={"universe-tab-panel"} className="universe-detail">
            { tabElements }
          </YBTabsPanel>
        </Measure>
      </Grid>
    );
  }
}

class UniverseTaskList extends Component {
  render() {
    const {universe: {universeTasks, currentUniverse}} = this.props;
    let universeTaskUUIDs = [];
    const universeTaskHistoryArray = [];
    let universeTaskHistory = <span/>;
    if (getPromiseState(universeTasks).isLoading()) {
      universeTaskHistory = <YBLoading />;
    }
    const currentUniverseTasks = universeTasks.data[currentUniverse.data.universeUUID];
    if (getPromiseState(universeTasks).isSuccess() && isNonEmptyObject(currentUniverse.data) && isNonEmptyArray(currentUniverseTasks)) {
      universeTaskUUIDs = currentUniverseTasks.map(function(task) {
        if (task.status !== "Running") {
          universeTaskHistoryArray.push(task);
        }
        return (task.status !== "Failure" && task.percentComplete !== 100) ? task.id : false;
      }).filter(Boolean);
    }
    if (isNonEmptyArray(universeTaskHistoryArray)) {
      universeTaskHistory = <TaskListTable taskList={universeTaskHistoryArray} title={"Task History"}/>;
    }
    let currentTaskProgress = <span/>;
    if (getPromiseState(universeTasks).isLoading()) {
      currentTaskProgress = <YBLoading/>;
    }
    else if (getPromiseState(universeTasks).isSuccess() && isNonEmptyArray(universeTaskUUIDs)) {
      currentTaskProgress = <TaskProgressContainer taskUUIDs={universeTaskUUIDs} type="StepBar"/>;
    }
    return (
      <div className="universe-detail-content-container">
        {currentTaskProgress}
        {universeTaskHistory}
      </div>
    );
  }
}

export default withRouter(mouseTrap(UniverseDetail));

// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Link, withRouter, browserHistory} from 'react-router';
import { Grid, Row, Col, ButtonGroup, DropdownButton, MenuItem, Tab } from 'react-bootstrap';
import Measure from 'react-measure';
import { UniverseInfoPanel, ResourceStringPanel } from '../../panels'
import { GraphPanelContainer, GraphPanelHeaderContainer } from '../../metrics';
import { TaskProgressContainer, TaskListTable } from '../../tasks';
import { RollingUpgradeFormContainer } from 'components/common/forms';
import { UniverseFormContainer, UniverseStatusContainer, NodeDetails,
         DeleteUniverseContainer, UniverseAppsModal } from '../../universes';
import { UniverseResources } from '../UniverseResources';
import { YBButton } from '../../common/forms/fields';
import { YBLabelWithIcon } from '../../common/descriptors';
import { YBTabsPanel } from '../../panels';
import { RegionMap } from '../../maps';
import { ListTablesContainer } from '../../tables';
import { YBMapLegend } from '../../maps';
import { isEmptyObject, isNonEmptyObject, isNonEmptyArray, isDefinedNotNull } from 'utils/ObjectUtils';
import {getPromiseState} from 'utils/PromiseUtils';
import {YBLoadingIcon} from '../../common/indicators';
import './UniverseDetail.scss';

class UniverseDetail extends Component {
  constructor(props) {
    super(props);
    this.onEditUniverseButtonClick = this.onEditUniverseButtonClick.bind(this);
    this.state = {
      dimensions: {},
    }
  }

  componentWillUnmount() {
    this.props.resetUniverseInfo();
    this.props.resetUniverseTasks();
  }

  componentWillMount() {
    if (this.props.location.pathname !== "/universes/create") {
      let uuid = this.props.uuid;
      if (typeof this.props.universeSelectionId !== "undefined") {
        uuid = this.props.universeUUID;
      }
      this.props.getUniverseInfo(uuid);
      this.props.fetchUniverseTasks(uuid);
    }
  }

  componentWillReceiveProps(nextProps) {
    if (getPromiseState(nextProps.universe.currentUniverse).isSuccess() && getPromiseState(this.props.universe.currentUniverse).isLoading()) {
      this.props.fetchAccessKeys(nextProps.universe.currentUniverse.data.provider.uuid);
    }
  }

  onResize(dimensions) {
    dimensions.width -= 40;
    this.setState({dimensions});
  }

  onEditUniverseButtonClick() {
    const location = Object.assign({}, browserHistory.getCurrentLocation());
    let query = {edit: true}
    Object.assign(location.query, query);
    browserHistory.push(location);
  }

  render() {
    const { universe: { currentUniverse, showModal, visibleModal }, universe, location: {query}} = this.props;

    if (this.props.location.pathname === "/universes/create") {
      return <UniverseFormContainer type="Create"/>
    }
    if (getPromiseState(currentUniverse).isLoading() || getPromiseState(currentUniverse).isInit()) {
      return <YBLoadingIcon/>
    } else if (isEmptyObject(currentUniverse.data)) {
      return <span />;
    }
    if (isNonEmptyObject(query) && query.edit) {
      return <UniverseFormContainer type="Edit" />
    }
    const width = this.state.dimensions.width;
    const nodePrefixes = [currentUniverse.data.universeDetails.nodePrefix];
    const graphPanelTypes = ['proxies', 'server', 'cql', 'redis', 'tserver', 'lsmdb'];
    const graphPanelContainers = graphPanelTypes.map(function (type, idx) {
      return <GraphPanelContainer key={idx} type={type} width={width} nodePrefixes={nodePrefixes} />
    });

    var tabElements = [
      <Tab eventKey={"overview"} title="Overview" key="overview-tab">
        <UniverseAppsModal nodeDetails={currentUniverse.data.universeDetails.nodeDetailsSet}/>
        {isDefinedNotNull(currentUniverse.data.resources) &&
          <UniverseResources resources={currentUniverse.data.resources} />
        }
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
            <YBMapLegend title="Placement Policy" regions={ currentUniverse.data.regions ? currentUniverse.data.regions : []}/>
          </Col>
        </Row>
      </Tab>,
      <Tab eventKey={"tables"} title="Tables" key="tables-tab">
        <ListTablesContainer/>
      </Tab>,
      <Tab eventKey={"nodes"} title="Nodes" key="nodes-tab">
        <NodeDetails nodeDetails={currentUniverse.data.universeDetails.nodeDetailsSet} />
      </Tab>,
      <Tab eventKey={"metrics"} title="Metrics" key="metrics-tab">
        <GraphPanelHeaderContainer origin={"universe"}>
          {graphPanelContainers}
        </GraphPanelHeaderContainer>
      </Tab>,
      <Tab eventKey={"tasks"} title="Tasks" key="tasks-tab">
        <UniverseTaskList universe={universe}/>
      </Tab>
    ];
    let currentBreadCrumb =
      <div className="detail-label-small">
        <Link to="/universes">
          <YBLabelWithIcon icon="fa fa-chevron-right fa-fw">
            Universes
          </YBLabelWithIcon>
        </Link>
        <Link to={`/universes/${currentUniverse.data.universeUUID}`}>
          <YBLabelWithIcon icon="fa fa-chevron-right fa-fw">
            {currentUniverse.data.name}
          </YBLabelWithIcon>
        </Link>
      </div>;

    return (
      <Grid id="page-wrapper" fluid={true}>
        <Row>
          <Col lg={10}>
            {currentBreadCrumb}
            <div className="universe-detail-status-container">
              <h2>
                { currentUniverse.data.name }
              </h2>
              <UniverseStatusContainer currentUniverse={currentUniverse.data} showLabelText={true} />
            </div>
          </Col>
          <Col lg={2} className="page-action-buttons">
            <ButtonGroup className="universe-detail-btn-group">
              <YBButton btnClass=" btn btn-default bg-orange"
                        btnText="Edit" btnIcon="fa fa-database" onClick={this.onEditUniverseButtonClick} />

              <DropdownButton className="btn btn-default" title="More" id="bg-nested-dropdown" pullRight>
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
            </ButtonGroup>
          </Col>
          <RollingUpgradeFormContainer modalVisible={showModal &&
          (visibleModal === "gFlagsModal" || visibleModal ==="softwareUpgradesModal")}
                                       onHide={this.props.closeModal} />
          <DeleteUniverseContainer visible={showModal && visibleModal==="deleteUniverseModal"}
                                   onHide={this.props.closeModal} title="Delete Universe"/>
        </Row>

        <Measure onMeasure={this.onResize.bind(this)}>
          <YBTabsPanel activeTab={"overview"} id={"universe-tab-panel"} className="universe-detail">
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
    var universeTaskUUIDs = [];
    var universeTaskHistoryArray = [];
    var universeTaskHistory = <span/>;
    if (getPromiseState(universeTasks).isLoading()) {
      universeTaskHistory = <YBLoadingIcon/>;
    }
    let currentUniverseTasks = universeTasks.data[currentUniverse.data.universeUUID];
    if (getPromiseState(universeTasks).isSuccess() && isNonEmptyObject(currentUniverse.data) && isNonEmptyArray(currentUniverseTasks)) {
      universeTaskUUIDs = currentUniverseTasks.map(function(task) {
        if (task.status !== "Running") {
          universeTaskHistoryArray.push(task);
        }
        return (task.status !== "Failure" && task.percentComplete !== 100) ? task.id : false;
      }).filter(Boolean);
    }
    if (isNonEmptyArray(universeTaskHistoryArray)) {
      universeTaskHistory = <TaskListTable taskList={universeTaskHistoryArray} title={"Task History"}/>
    }
    let currentTaskProgress = <span/>;
    if (isNonEmptyArray) {
      currentTaskProgress = <TaskProgressContainer taskUUIDs={universeTaskUUIDs} type="StepBar"/>;
    }
    return (
      <div>
        {currentTaskProgress}
        {universeTaskHistory}
      </div>
    )
  }
}

export default withRouter(UniverseDetail);

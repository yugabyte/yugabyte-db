// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Link, withRouter, browserHistory} from 'react-router';
import { Grid, DropdownButton, MenuItem, Tab } from 'react-bootstrap';
import Measure from 'react-measure';
import { CustomerMetricsPanel } from '../../metrics';
import { TaskProgressContainer, TaskListTable } from '../../tasks';
import { RollingUpgradeFormContainer } from 'components/common/forms';
import { UniverseFormContainer, UniverseStatusContainer, NodeDetailsContainer,
         DeleteUniverseContainer, UniverseAppsModal, UniverseConnectModal, UniverseOverviewContainerNew } from '../../universes';
import { YBLabelWithIcon } from '../../common/descriptors';
import { YBTabsPanel } from '../../panels';
import { ListTablesContainer, ListBackupsContainer } from '../../tables';
import { isEmptyObject, isNonEmptyObject, isNonEmptyArray, isEmptyArray } from '../../../utils/ObjectUtils';
import { isKubernetesUniverse } from '../../../utils/UniverseUtils';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { hasLiveNodes } from 'utils/UniverseUtils';

import { YBLoading, YBErrorIndicator } from '../../common/indicators';
import { mouseTrap } from 'react-mousetrap';
import { TASK_SHORT_TIMEOUT } from '../../tasks/constants';
import UniverseHealthCheckList from './UniverseHealthCheckList/UniverseHealthCheckList.js';
import { isNonAvailable, isDisabled, isEnabled, isHidden, isNotHidden, getFeatureState } from 'utils/LayoutUtils';

import './UniverseDetail.scss';

class UniverseDetail extends Component {
  constructor(props) {
    super(props);
    this.onEditReadReplicaButtonClick = this.onEditReadReplicaButtonClick.bind(this);
    this.showUpgradeMarker = this.showUpgradeMarker.bind(this);
    this.state = {
      dimensions: {},
    };
  }

  hasReadReplica = (universeInfo) => {
    const clusters = universeInfo.universeDetails.clusters;
    return clusters.some((cluster) => cluster.clusterType === "ASYNC");
  }

  componentWillUnmount() {
    this.props.resetUniverseInfo();
    this.props.resetTablesList();
  }

  componentWillMount() {
    const { customer : { currentCustomer }} = this.props;
    if (isNonAvailable(currentCustomer.data.features, "universes.details")) {
      if (isNonAvailable(currentCustomer.data.features, "universes")) {
        browserHistory.push('/');
      } else {
        browserHistory.push('/universes/');
      }
    }

    this.props.bindShortcut(['ctrl+e', 'e'], this.onEditUniverseButtonClick);

    if (this.props.location.pathname !== "/universes/create") {
      let uuid = this.props.uuid;
      if (typeof this.props.universeSelectionId !== "undefined") {
        uuid = this.props.universeUUID;
      }
      this.props.getUniverseInfo(uuid);
      this.props.getHealthCheck(uuid);
    }
  }

  componentDidUpdate(prevProps, prevState, snapshot) {
    const { universe: { currentUniverse } } = this.props;
    if (getPromiseState(currentUniverse).isSuccess() &&
        !getPromiseState(prevProps.universe.currentUniverse).isSuccess()) {
      if (hasLiveNodes(currentUniverse.data)) {
        this.props.fetchUniverseTables(currentUniverse.data.universeUUID);
      }
    }
  }

  onResize(dimensions) {
    this.setState({dimensions});
  }

  onEditUniverseButtonClick = () => {
    const location = Object.assign({}, browserHistory.getCurrentLocation());
    const query = {edit: true};
    Object.assign(location.query, query);
    browserHistory.push(location);
  };

  onEditReadReplicaButtonClick = () => {
    const location = Object.assign({}, browserHistory.getCurrentLocation());
    const query = {edit: true, async: true};
    Object.assign(location.query, query);
    browserHistory.push(location);
  };

  getUniverseInfo = () => {
    const universeUUID = this.props.universe.currentUniverse.data.universeUUID;
    this.props.getUniverseInfo(universeUUID);
  };

  showUpgradeMarker = () => {
    const { updateAvailable, universe: { rollingUpgrade }, modal: { showModal, visibleModal }} = this.props;

    if (!getPromiseState(rollingUpgrade).isLoading() &&
        updateAvailable &&
        !(showModal && visibleModal ==="softwareUpgradesModal")) {
      return true;
    }
    return false;
  };

  transitToDefaultRoute = () => {
    const currentLocation = this.props.location;
    currentLocation.query = currentLocation.query.tab ? {tab: currentLocation.query.tab} : {};
    this.props.router.push(currentLocation);
  }

  render() {
    const {
      uuid,
      updateAvailable,
      modal,
      modal: { showModal, visibleModal },
      universe,
      tasks,
      universe: { currentUniverse },
      location: { query, pathname },
      showSoftwareUpgradesModal,
      showRunSampleAppsModal,
      showGFlagsModal,
      showDeleteUniverseModal,
      closeModal,
      customer,
      customer: { currentCustomer }
    } = this.props;

    const isReadOnlyUniverse = getPromiseState(currentUniverse).isSuccess() && currentUniverse.data.universeDetails.capability === "READ_ONLY";

    if (pathname === "/universes/create") {
      return <UniverseFormContainer type="Create"/>;
    }
    if (getPromiseState(currentUniverse).isLoading() || getPromiseState(currentUniverse).isInit()) {
      return <YBLoading />;
    } else if (isEmptyObject(currentUniverse.data)) {
      return <span />;
    }
    if (isNonEmptyObject(query) && query.edit && query.async) {
      if (isReadOnlyUniverse) {
        // not fully legit but mandatory fallback for manually edited query
        this.transitToDefaultRoute();
      }
      else {
        return <UniverseFormContainer type="Async" />;
      }
    }
    if (isNonEmptyObject(query) && query.edit) {
      if (isReadOnlyUniverse) {
        // not fully legit but mandatory fallback for manually edited query
        this.transitToDefaultRoute();
      }
      else {
        return <UniverseFormContainer type="Edit" />;
      }
    }

    if (getPromiseState(currentUniverse).isError()) {
      return <YBErrorIndicator type="universe" uuid={uuid}/>;
    }

    const width = this.state.dimensions.width;
    const universeInfo = currentUniverse.data;
    const nodePrefixes = [currentUniverse.data.universeDetails.nodePrefix];
    const isItKubernetesUniverse = isKubernetesUniverse(currentUniverse.data);
    const tabElements = [
      //common tabs for every universe
      ...[
        isNotHidden(currentCustomer.data.features, "universes.details.overview") &&
          <Tab eventKey={"overview"} title="Overview" key="overview-tab" mountOnEnter={true} unmountOnExit={true} disabled={isDisabled(currentCustomer.data.features, "universes.details.overview")}>
            <UniverseOverviewContainerNew width={width} universe={universe} updateAvailable={updateAvailable} showSoftwareUpgradesModal={showSoftwareUpgradesModal} />
          </Tab>,

        isNotHidden(currentCustomer.data.features, "universes.details.tables") &&
          <Tab eventKey={"tables"} title="Tables" key="tables-tab" mountOnEnter={true} unmountOnExit={true} disabled={isDisabled(currentCustomer.data.features, "universes.details.tables")}>
            <ListTablesContainer/>
          </Tab>,

        isNotHidden(currentCustomer.data.features, "universes.details.nodes") &&
          <Tab eventKey={"nodes"} title={isItKubernetesUniverse ? "Pods" : "Nodes"} key="nodes-tab" mountOnEnter={true} unmountOnExit={true} disabled={isDisabled(currentCustomer.data.features, "universes.details.nodes")}>
            <NodeDetailsContainer />
          </Tab>,

        isNotHidden(currentCustomer.data.features, "universes.details.metrics") &&
          <Tab eventKey={"metrics"} title="Metrics" key="metrics-tab" mountOnEnter={true} unmountOnExit={true} disabled={isDisabled(currentCustomer.data.features, "universes.details.metrics")}>
            <div className="universe-detail-content-container">
              <CustomerMetricsPanel customer={customer} origin={"universe"} width={width} nodePrefixes={nodePrefixes} isKubernetesUniverse={isItKubernetesUniverse} />
            </div>
          </Tab>,

        isNotHidden(currentCustomer.data.features, "universes.details.tasks") &&
          <Tab eventKey={"tasks"} title="Tasks" key="tasks-tab" mountOnEnter={true} unmountOnExit={true} disabled={isDisabled(currentCustomer.data.features, "universes.details.tasks")} >
            <UniverseTaskList universe={universe} tasks={tasks} />
          </Tab>
      ],
      //tabs relevant for non-imported universes only
      ...isReadOnlyUniverse ? [] : [
        isNotHidden(currentCustomer.data.features, "universes.details.backups") &&
          <Tab eventKey={"backups"} title="Backups" key="backups-tab" mountOnEnter={true} unmountOnExit={true} disabled={isDisabled(currentCustomer.data.features, "universes.details.backups")}>
            <ListBackupsContainer currentUniverse={currentUniverse.data} />
          </Tab>,

        isNotHidden(currentCustomer.data.features, "universes.details.health") &&
          <Tab eventKey={"health"} title="Health" key="health-tab" mountOnEnter={true} unmountOnExit={true} disabled={isDisabled(currentCustomer.data.features, "universes.details.heath")}>
            <UniverseHealthCheckList universe={universe} />
          </Tab>
      ]
    ].filter(element => element);
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
      <Grid id="page-wrapper" fluid={true} className={`universe-details universe-details-new`}>
        {/* UNIVERSE NAME */}
        {currentBreadCrumb}
        <div className="universe-detail-status-container">
          <h2>
            { currentUniverse.data.name }
          </h2>
          <UniverseStatusContainer currentUniverse={currentUniverse.data} showLabelText={true} refreshUniverseData={this.getUniverseInfo}/>
        </div>
        <div className="page-action-buttons">

          {/* UNIVERSE EDIT */}
          <div className="universe-detail-btn-group">

            <UniverseConnectModal/> 

            <DropdownButton title="More" className={this.showUpgradeMarker() ? "btn-marked": ""} id="bg-nested-dropdown" pullRight>
              <YBMenuItem eventKey="1" onClick={showSoftwareUpgradesModal} availability={getFeatureState(currentCustomer.data.features, "universes.details.overview.upgradeSoftware")}>
                <YBLabelWithIcon icon="fa fa-arrow-up fa-fw">
                  Upgrade Software
                </YBLabelWithIcon>
                { this.showUpgradeMarker() ? <span className="badge badge-pill badge-red pull-right">{updateAvailable}</span> : ""}
              </YBMenuItem>
              {!isReadOnlyUniverse && isNotHidden(currentCustomer.data.features, "universes.details.metrics") && 
                <YBMenuItem eventKey="2" onClick={this.onEditUniverseButtonClick} availability={getFeatureState(currentCustomer.data.features, "universes.details.metrics")}>
                  <YBLabelWithIcon icon="fa fa-pencil">
                  Edit Universe
                  </YBLabelWithIcon>
                </YBMenuItem>
              }
              <YBMenuItem eventKey="4" onClick={showGFlagsModal} availability={getFeatureState(currentCustomer.data.features, "universes.details.overview.editGFlags")}>
                <YBLabelWithIcon icon="fa fa-flag fa-fw">
                  Edit GFlags
                </YBLabelWithIcon>
              </YBMenuItem>
              {!isReadOnlyUniverse &&
                <YBMenuItem eventKey="3" onClick={this.onEditReadReplicaButtonClick} availability={getFeatureState(currentCustomer.data.features, "universes.details.overview.readReplica")}>
                  <YBLabelWithIcon icon="fa fa-copy fa-fw">
                    {this.hasReadReplica(universeInfo) ? "Edit" : "Add" } Read Replica
                  </YBLabelWithIcon>
                  <span className="badge badge-pill badge-blue pull-right">Beta</span>
                </YBMenuItem>
              }
              <UniverseAppsModal currentUniverse={currentUniverse.data} modal={modal} closeModal={closeModal} button={
                <YBMenuItem eventKey="0" onClick={showRunSampleAppsModal}>
                  <YBLabelWithIcon icon="fa fa-terminal">
                    Run Sample Apps
                  </YBLabelWithIcon>
                </YBMenuItem>
              }/>
              <div className="divider"></div>
              <YBMenuItem eventKey="5" onClick={showDeleteUniverseModal} availability={getFeatureState(currentCustomer.data.features, "universes.details.overview.deleteUniverse")}>
                <YBLabelWithIcon icon="fa fa-trash-o fa-fw">
                  Delete Universe
                </YBLabelWithIcon>
              </YBMenuItem>
            </DropdownButton>
          </div>
        </div>
        <RollingUpgradeFormContainer modalVisible={showModal &&
        (visibleModal === "gFlagsModal" || visibleModal ==="softwareUpgradesModal")}
                                      onHide={closeModal} />
        <DeleteUniverseContainer visible={showModal && visibleModal==="deleteUniverseModal"}
                                  onHide={closeModal} title="Delete Universe: " body="Are you sure you want to delete the universe? You will lose all your data!" type="primary"/>

        <Measure onMeasure={this.onResize.bind(this)}>
          <YBTabsPanel defaultTab={"overview"} id={"universe-tab-panel"} className="universe-detail">
            { tabElements }
          </YBTabsPanel>
        </Measure>
      </Grid>
    );
  }
}

class YBMenuItem extends Component {
  render() {
    const { availability, className, onClick } = this.props;
    if (isHidden(availability) && availability !== undefined) return null;
    if (isEnabled(availability)) return (
      <MenuItem className={className} onClick={onClick}>
        {this.props.children}
      </MenuItem>
    );
    return (
      <li>
        <div className={className}>
          {this.props.children}
        </div>
      </li>
    );
  }
}

class UniverseTaskList extends Component {
  tasksForUniverse = () => {
    const { universe: { currentUniverse: { data: { universeUUID }}}, tasks: { customerTaskList }} = this.props;
    const resultTasks = [];
    if(isNonEmptyArray(customerTaskList)) {
      customerTaskList.forEach((taskItem) => {
        if (taskItem.targetUUID === universeUUID) resultTasks.push(taskItem);
      });
    };
    return resultTasks;
  };

  render() {
    const {universe: {currentUniverse}, tasks: {customerTaskList}} = this.props;
    const currentUniverseTasks = this.tasksForUniverse();
    let universeTaskUUIDs = [];
    const universeTaskHistoryArray = [];
    let universeTaskHistory = <span/>;
    let currentTaskProgress = <span/>;
    if (isEmptyArray(customerTaskList)) {
      universeTaskHistory = <YBLoading />;
      currentTaskProgress = <YBLoading />;
    }
    if (isNonEmptyArray(customerTaskList) && isNonEmptyObject(currentUniverse.data) && isNonEmptyArray(currentUniverseTasks)) {
      universeTaskUUIDs = currentUniverseTasks.map(function(task) {
        universeTaskHistoryArray.push(task);
        return (task.status !== "Failure" && task.percentComplete !== 100) ? task.id : false;
      }).filter(Boolean);
    }
    if (isNonEmptyArray(universeTaskHistoryArray)) {
      universeTaskHistory = <TaskListTable taskList={universeTaskHistoryArray} title={"Task History"}/>;
    }
    if (isNonEmptyArray(customerTaskList) && isNonEmptyArray(universeTaskUUIDs)) {
      currentTaskProgress = <TaskProgressContainer taskUUIDs={universeTaskUUIDs} type="StepBar" timeoutInterval={TASK_SHORT_TIMEOUT}/>;
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

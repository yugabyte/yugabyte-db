// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { Row, Col, Grid } from 'react-bootstrap';
import { change, Fields } from 'redux-form';
import {browserHistory, withRouter} from 'react-router';
import _ from 'lodash';
import { isDefinedNotNull, isNonEmptyObject, isNonEmptyString, areIntentsEqual, isNonEmptyArray,
  normalizeToPositiveFloat } from 'utils/ObjectUtils';
import {YBButton } from 'components/common/forms/fields';
import { UniverseResources } from '../UniverseResources';
import './UniverseForm.scss';
import { IN_DEVELOPMENT_MODE } from '../../../config';
import {getPrimaryCluster} from "../../../utils/UniverseUtils";

import ClusterFields from './ClusterFields';

const initialState = {
  instanceTypeSelected: '',
  nodeSetViaAZList: false,
  placementInfo: {},
  useSpotPrice: IN_DEVELOPMENT_MODE,
  spotPrice: normalizeToPositiveFloat('0.00'),
  gettingSuggestedSpotPrice: false,
  currentView: 'primary',
};

class UniverseForm extends Component {
  static propTypes = {
    type: PropTypes.oneOf(['Edit', 'Create']).isRequired
  };

  constructor(props, context) {
    super(props);
    this.createUniverse = this.createUniverse.bind(this);
    this.editUniverse = this.editUniverse.bind(this);
    this.handleCancelButtonClick = this.handleCancelButtonClick.bind(this);
    this.handleSubmitButtonClick = this.handleSubmitButtonClick.bind(this);
    this.handleUniverseConfigure = this.handleUniverseConfigure.bind(this);
    this.getFormPayload = this.getFormPayload.bind(this);
    this.configureAsyncCluster = this.configureAsyncCluster.bind(this);
    this.configurePrimaryCluster = this.configurePrimaryCluster.bind(this);
    this.updateFormField = this.updateFormField.bind(this);
    this.state = initialState;
  }


  updateFormField(fieldName, fieldValue) {
    this.props.dispatch(change("UniverseForm", fieldName, fieldValue));
  }

  configureAsyncCluster() {
    this.setState({currentView: 'async'});
  }

  configurePrimaryCluster() {
    this.setState({currentView: 'primary'});
  }

  handleCancelButtonClick() {
    this.setState(initialState);
    this.props.reset();
    if (this.props.type === "Create") {
      if (this.context.prevPath) {
        browserHistory.push(this.context.prevPath);
      } else {
        browserHistory.push("/universes");
      }
    } else {
      if (this.props.location && this.props.location.pathname) {
        browserHistory.push(this.props.location.pathname);
      }
    }
  }

  handleSubmitButtonClick() {
    const {type} = this.props;
    if (type === "Create") {
      this.createUniverse();
    } else {
      this.editUniverse();
    }
  }

  handleUniverseConfigure(universeTaskParams) {
    const {universe: {currentUniverse}, type} = this.props;
    const primaryCluster = getPrimaryCluster(universeTaskParams.clusters);
    if (!isNonEmptyObject(primaryCluster)) return;
    const checkSpotPrice = primaryCluster.userIntent.providerType === 'aws' && !this.state.gettingSuggestedSpotPrice;
    if (isDefinedNotNull(this.state.instanceTypeSelected) && isNonEmptyArray(this.state.regionList) &&
      (!checkSpotPrice || _.isEqual(this.state.spotPrice.toString(), normalizeToPositiveFloat(this.state.spotPrice.toString())) || type === "Edit")) {
      if (isNonEmptyObject(currentUniverse.data) && isNonEmptyObject(currentUniverse.data.universeDetails)) {
        const prevPrimaryCluster = getPrimaryCluster(currentUniverse.data.universeDetails.clusters);
        const nextPrimaryCluster = getPrimaryCluster(universeTaskParams.clusters);
        if (isNonEmptyObject(prevPrimaryCluster) && isNonEmptyObject(nextPrimaryCluster) &&
          areIntentsEqual(prevPrimaryCluster.userIntent, nextPrimaryCluster.userIntent)) {
          this.props.getExistingUniverseConfiguration(currentUniverse.data.universeDetails);
        } else {
          this.props.submitConfigureUniverse(universeTaskParams);
        }
      } else {
        this.props.submitConfigureUniverse(universeTaskParams);
      }
    }
  }

  createUniverse() {
    this.props.submitCreateUniverse(this.getFormPayload());
  }

  editUniverse() {
    const {universe: {currentUniverse: {data: {universeUUID}}}} = this.props;
    this.props.submitEditUniverse(this.getFormPayload(), universeUUID);
  }

  componentWillMount() {
    this.props.resetConfig();
  }

  componentWillUpdate(newProps) {
    if (newProps.universe.formSubmitSuccess) {
      this.props.reset();
    }
  }

  getFormPayload() {
    const {formValues, universe: {universeConfigTemplate}} = this.props;
    const submitPayload = _.clone(universeConfigTemplate.data, true);
    const getIntentValues = function (clusterType) {
      const clusterIntent = {
        regionList: formValues[clusterType].regionList.map(function (item) {
          return item.value;
        }),
        provider: formValues[clusterType].provider,
        instanceType: formValues[clusterType].instanceType,
        numNodes: formValues[clusterType].numNodes,
        accessKeyCode: formValues[clusterType].accessKeyCode,
        replicationFactor: formValues[clusterType].replicationFactor,
        ybSoftwareVersion: formValues[clusterType].ybSoftwareVersion,
        deviceInfo: {
          volumeSize: formValues[clusterType].volumeSize,
          numVolumes: formValues[clusterType].numVolumes,
          diskIops: formValues[clusterType].diskIops,
          mountPoints: formValues[clusterType].mountPoints,
          ebsType: formValues[clusterType].ebsType
        },
        spotPrice: formValues[clusterType].spotPrice
      };
      if (clusterType === "primary") {
        clusterIntent.universeName = formValues[clusterType].universeName;
        clusterIntent.masterGFlags = formValues.primary.masterGFlags.filter((masterFlag) => {
          return isNonEmptyString(masterFlag.name) && isNonEmptyString(masterFlag.value);
        }).map((masterFlag) => {
          return {name: masterFlag.name, value: masterFlag.value};
        });
        clusterIntent.tserverGFlags = formValues.primary.tserverGFlags.filter((tserverFlag) => {
          return isNonEmptyString(tserverFlag.name) && isNonEmptyString(tserverFlag.value);
        }).map((tserverFlag) => {
          return {name: tserverFlag.name, value: tserverFlag.value};
        });
      }
      return clusterIntent;
    };

    let asyncClusterFound = false;
    if (isNonEmptyArray(submitPayload.clusters)) {
      submitPayload.clusters.forEach(function (cluster, idx, arr) {
        if (cluster.clusterType === "PRIMARY") {
          submitPayload.clusters[idx].userIntent = getIntentValues("primary");
        }
        if (cluster.clusterType === "ASYNC" && isNonEmptyObject(formValues.async)) {
          asyncClusterFound = true;
          submitPayload.clusters[idx].userIntent = getIntentValues("async");
        }
      });

      // If async cluster array is not set then set it
      if (isNonEmptyObject(formValues.async) && !asyncClusterFound) {
        submitPayload.clusters.push({
          clusterType: "ASYNC",
          userIntent: getIntentValues("async")
        });
      }
    } else {
      submitPayload.clusters = [
        {
          clusterType: "PRIMARY",
          userIntent: getIntentValues("primary")
        },
        {
          clusterType: "ASYNC",
          userIntent: getIntentValues("async")
        }
      ]
    }
    return submitPayload;
  }

  render() {
    const {handleSubmit, universe, softwareVersions, cloud,  getInstanceTypeListItems, submitConfigureUniverse, type,
      getRegionListItems, resetConfig, formValues, getSuggestedSpotPrice, fetchUniverseResources, resetSuggestedSpotPrice, location: {search}} = this.props;
    // TODO Remove this barrier once async feature committed to mainline
    let displayPane = "primary";
    if (search === "?new") {
      displayPane = "async";
    }

    let primaryCell = "";
    let asyncCell = "";
    if (this.state.currentView === "primary") {
      primaryCell = "active-stepper-cell";
      asyncCell = "";
    } else {
      primaryCell = "";
      asyncCell = "active-stepper-cell";
    }
    const createUniverseTitle =
      (<div className="universe-heading-title">
        <Row className="stepper-container">
          <Col lg={2}>
            <div className="inline-heading">{this.props.type} universe</div>
          </Col>
          <Col lg={2} className={`stepper-cell ${primaryCell}`}>
            1. Primary Cluster
          </Col>
          {displayPane === "async" ?
            <Col lg={2} className={`stepper-cell ${asyncCell}`}>
              2. Read Replica
            </Col> : <span/>
          }
        </Row>
      </div>);

    let primaryUniverseName = "";
    if (this.props.formValues && this.props.formValues.primary) {
      primaryUniverseName = this.props.formValues.primary.universeName;
    }

    const pageTitle = this.props.type === "Create" ? createUniverseTitle : <h2 className="content-title">{primaryUniverseName}<span> - {this.props.type.toLowerCase()} universe </span></h2>;
    let clusterForm = <span/>;
    let primaryReplicaBtn = <span/>;
    let asyncReplicaBtn = <span/>;

    if (this.state.currentView === "async" && type !== "Edit" && displayPane === "async") {
      primaryReplicaBtn = <YBButton btnClass="btn btn-default universe-form-submit-btn" btnText={"Previous"} onClick={this.configurePrimaryCluster}/>;
    }

    if (this.state.currentView === "primary" && type !== "Edit" && displayPane === "async") {
      asyncReplicaBtn = <YBButton btnClass="btn btn-default universe-form-submit-btn" btnText={"Next: Configure Read Replica"} onClick={this.configureAsyncCluster}/>;
    }
    let submitTextLabel = "";
    if (type === "Create") {
      submitTextLabel = "Create";
    } else {
      submitTextLabel = "Edit";
    };

    const clusterProps = {
      universe: universe, getRegionListItems: getRegionListItems,
      getInstanceTypeListItems: getInstanceTypeListItems, cloud: cloud, resetConfig: resetConfig,
      accessKeys: this.props.accessKeys, softwareVersions: softwareVersions, updateFormField: this.updateFormField,
      formValues: formValues, submitConfigureUniverse: submitConfigureUniverse, setPlacementStatus: this.props.setPlacementStatus,
      getSuggestedSpotPrice: getSuggestedSpotPrice, fetchUniverseResources: fetchUniverseResources,
      resetSuggestedSpotPrice: resetSuggestedSpotPrice, reset: this.props.reset, fetchUniverseMetadata: this.props.fetchUniverseMetadata,
      fetchCustomerTasks: this.props.fetchCustomerTasks, type: type, getExistingUniverseConfiguration: this.props.getExistingUniverseConfiguration
    };

    if (this.state.currentView === "primary") {
      clusterForm = (<PrimaryClusterFields {...clusterProps}/>);
    } else {
      // show async cluster if view if async
      clusterForm = (<AsyncClusterFields {...clusterProps}/>);
    }

    return (
      <Grid id="page-wrapper" fluid={true} className="universe-form-new">
        {pageTitle}
        <form name="UniverseForm" className="universe-form-container" onSubmit={handleSubmit(this.handleSubmitButtonClick)}>
          {clusterForm}
          <div className="form-action-button-container">
            <UniverseResources resources={universe.universeResourceTemplate.data}>
              {primaryReplicaBtn}
              <div className="form-cancel-btn" onClick={this.handleCancelButtonClick}>Cancel</div>
              <YBButton btnClass="btn btn-orange universe-form-submit-btn" btnText={submitTextLabel} btnType={"submit"}/>
              {asyncReplicaBtn}
            </UniverseResources>
          </div>
        </form>
      </Grid>
    );
  }
}

UniverseForm.contextTypes = {
  prevPath: PropTypes.string
};


class PrimaryClusterFields extends Component {
  render() {
    return (
      <Fields names={['primary.universeName', 'primary.provider', 'primary.providerType', 'primary.regionList', 'primary.replicationFactor',
        'primary.numNodes', 'primary.instanceType', 'primary.masterGFlags', 'primary.tserverGFlags', 'primary.ybSoftwareVersion',
        'primary.diskIops', 'primary.numVolumes', 'primary.volumeSize', 'primary.ebsType', 'primary.spotPrice']}
              component={ClusterFields} {...this.props} clusterType={"primary"} />
    );
  }
}

class AsyncClusterFields extends Component {
  render() {
    return (
      <Fields names={['async.provider', 'async.providerType', 'async.regionList', 'async.replicationFactor',
        'async.numNodes', 'async.instanceType', 'async.ybSoftwareVersion', 'async.diskIops','async.numVolumes','async.volumeSize',
        'async.ebsType', 'async.spotPrice']}
              component={ClusterFields} {...this.props} clusterType={"async"}/>
    );
  }
}

export default withRouter(UniverseForm);

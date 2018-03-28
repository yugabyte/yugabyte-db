// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { Grid } from 'react-bootstrap';
import { change, Fields } from 'redux-form';
import {browserHistory, withRouter} from 'react-router';
import _ from 'lodash';
import { isNonEmptyObject, isNonEmptyString, isNonEmptyArray, normalizeToPositiveFloat } from 'utils/ObjectUtils';
import {YBButton } from 'components/common/forms/fields';
import { UniverseResources } from '../UniverseResources';
import { FlexContainer, FlexShrink } from '../../common/flexbox/YBFlexBox';
import './UniverseForm.scss';
import { IN_DEVELOPMENT_MODE } from '../../../config';
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
    this.getFormPayload = this.getFormPayload.bind(this);
    this.configureReadOnlyCluster = this.configureReadOnlyCluster.bind(this);
    this.configurePrimaryCluster = this.configurePrimaryCluster.bind(this);
    this.updateFormField = this.updateFormField.bind(this);
    this.getCurrentProvider = this.getCurrentProvider.bind(this);
    this.state = initialState;
  }

  getCurrentProvider(providerUUID) {
    return this.props.cloud.providers.data.find((provider) => provider.uuid === providerUUID);
  }

  updateFormField = (fieldName, fieldValue) => {
    this.props.dispatch(change("UniverseForm", fieldName, fieldValue));
  }

  configureReadOnlyCluster = () => {
    this.setState({currentView: 'async'});
  }

  configurePrimaryCluster = () => {
    this.setState({currentView: 'primary'});
  }

  handleCancelButtonClick = () => {
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

  handleSubmitButtonClick = () => {
    const {type} = this.props;
    if (type === "Create") {
      this.createUniverse();
    } else {
      this.editUniverse();
    }
  }

  createUniverse = () => {
    this.props.submitCreateUniverse(this.getFormPayload());
  }

  editUniverse = () => {
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

  getFormPayload = () => {
    const {formValues, universe: {universeConfigTemplate}} = this.props;
    const submitPayload = _.clone(universeConfigTemplate.data, true);
    const self = this;
    const getIntentValues = function (clusterType) {
      const clusterIntent = {
        regionList: formValues[clusterType].regionList.map(function (item) {
          return item.value;
        }),
        provider: formValues[clusterType].provider,
        assignPublicIP: formValues[clusterType].assignPublicIP,
        providerType: self.getCurrentProvider(formValues[clusterType].provider).code,
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
        spotPrice: normalizeToPositiveFloat(formValues[clusterType].spotPrice)
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
      ];
    }
    return submitPayload;
  }

  render() {
    const {handleSubmit, universe, softwareVersions, cloud,  getInstanceTypeListItems, submitConfigureUniverse, type,
      getRegionListItems, resetConfig, formValues, getSuggestedSpotPrice, fetchUniverseResources, fetchNodeInstanceList,
      resetSuggestedSpotPrice} = this.props;
    const createUniverseTitle =
      (<h2 className="content-title">
        <FlexContainer>
          <FlexShrink>
            <div>{this.props.type} universe</div>
          </FlexShrink>
          <FlexShrink className={this.state.currentView === "primary" ? 'stepper-cell active-stepper-cell' : 'stepper-cell'}>
            1. Primary Cluster
          </FlexShrink>
          <FlexShrink className={this.state.currentView === "primary" ? 'stepper-cell' : 'stepper-cell active-stepper-cell'}>
            2. Read Replica
          </FlexShrink>
        </FlexContainer>
      </h2>);

    let primaryUniverseName = "";
    if (this.props.formValues && this.props.formValues.primary) {
      primaryUniverseName = this.props.formValues.primary.universeName;
    }

    const pageTitle = this.props.type === "Create" ? createUniverseTitle : <h2 className="content-title">{primaryUniverseName}<span> - {this.props.type.toLowerCase()} universe </span></h2>;
    let clusterForm = <span/>;
    let primaryReplicaBtn = <span/>;
    let asyncReplicaBtn = <span/>;
    let isReadOnly = false;

    if (this.state.currentView === "async" && type !== "Edit") {
      primaryReplicaBtn = <YBButton btnClass="btn btn-default universe-form-submit-btn" btnText={"Previous"} onClick={this.configurePrimaryCluster}/>;
    }

    if (this.state.currentView === "primary" && type !== "Edit") {
      asyncReplicaBtn = <YBButton btnClass="btn btn-default universe-form-submit-btn" btnText={"Next: Configure Read Replica"} onClick={this.configureReadOnlyCluster}/>;
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
      getSuggestedSpotPrice: getSuggestedSpotPrice, fetchUniverseResources: fetchUniverseResources, fetchUniverseTasks: this.props.fetchUniverseTasks,
      fetchNodeInstanceList: fetchNodeInstanceList,
      resetSuggestedSpotPrice: resetSuggestedSpotPrice, reset: this.props.reset, fetchUniverseMetadata: this.props.fetchUniverseMetadata,
      fetchCustomerTasks: this.props.fetchCustomerTasks, type: type, getExistingUniverseConfiguration: this.props.getExistingUniverseConfiguration,
      fetchCurrentUniverse: this.props.fetchCurrentUniverse, location: this.props.location
    };

    if (this.state.currentView === "primary") {
      if (type === "edit") {
        isReadOnly = true;
      }
      clusterForm = (<PrimaryClusterFields {...clusterProps} isFieldReadOnly={isReadOnly}/>);
    } else {
      // show async cluster if view if async
      clusterForm = (<ReadOnlyClusterFields {...clusterProps}/>);
    }

    return (
      <Grid id="page-wrapper" fluid={true} className="universe-form-new">
        {pageTitle}
        <form name="UniverseForm" className="universe-form-container" onSubmit={handleSubmit(this.handleSubmitButtonClick)}>
          {clusterForm}
          <div className="form-action-button-container">
            <UniverseResources resources={universe.universeResourceTemplate.data}>
              {primaryReplicaBtn}
              <YBButton btnClass="btn btn-default universe-form-submit-btn" btnText="Cancel" onClick={this.handleCancelButtonClick}/>
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
        'primary.diskIops', 'primary.numVolumes', 'primary.volumeSize', 'primary.ebsType', 'primary.spotPrice', 'primary.useSpotPrice',
        'primary.assignPublicIP']} component={ClusterFields} {...this.props} clusterType={"primary"} />
    );
  }
}

class ReadOnlyClusterFields extends Component {
  render() {
    return (
      <Fields names={['async.provider', 'async.providerType', 'async.regionList', 'async.replicationFactor',
        'async.numNodes', 'async.instanceType', 'async.ybSoftwareVersion', 'async.diskIops',
        'async.numVolumes','async.volumeSize', 'async.spotPrice', 'async.useSpotPrice',
        'async.ebsType', 'async.assignPublicIP']}
              component={ClusterFields} {...this.props} clusterType={"async"}/>
    );
  }
}

export default withRouter(UniverseForm);

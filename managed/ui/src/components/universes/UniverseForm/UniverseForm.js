// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { Grid } from 'react-bootstrap';
import { change, Fields } from 'redux-form';
import {browserHistory, withRouter} from 'react-router';
import _ from 'lodash';
import { isNonEmptyObject, isDefinedNotNull, isNonEmptyString, isNonEmptyArray, normalizeToPositiveFloat } from 'utils/ObjectUtils';
import {YBButton } from 'components/common/forms/fields';
import { UniverseResources } from '../UniverseResources';
import { FlexContainer, FlexShrink } from '../../common/flexbox/YBFlexBox';
import './UniverseForm.scss';
import { IN_DEVELOPMENT_MODE } from '../../../config';
import ClusterFields from './ClusterFields';
import { getPrimaryCluster, getReadOnlyCluster } from "../../../utils/UniverseUtils";
import { DeleteUniverseContainer } from '../../universes';

const initialState = {
  instanceTypeSelected: '',
  nodeSetViaAZList: false,
  placementInfo: {},
  useSpotPrice: IN_DEVELOPMENT_MODE,
  spotPrice: normalizeToPositiveFloat('0.00'),
  gettingSuggestedSpotPrice: false,
  currentView: 'Primary',
};

class UniverseForm extends Component {
  static propTypes = {
    type: PropTypes.oneOf(['Async', 'Edit', 'Create']).isRequired,
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
    this.editReadReplica = this.editReadReplica.bind(this);
    this.addReadReplica = this.addReadReplica.bind(this);
    this.updateFormField = this.updateFormField.bind(this);
    this.getCurrentProvider = this.getCurrentProvider.bind(this);
    this.state = {
      ...initialState,
      currentView: props.type === "Async" ? 'Async' : 'Primary'
    };
  }

  getCurrentProvider(providerUUID) {
    return this.props.cloud.providers.data.find((provider) => provider.uuid === providerUUID);
  }

  updateFormField = (fieldName, fieldValue) => {
    this.props.dispatch(change("UniverseForm", fieldName, fieldValue));
  }

  configureReadOnlyCluster = () => {
    this.setState({currentView: 'Async'});
  }

  configurePrimaryCluster = () => {
    this.setState({currentView: 'Primary'});
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
    const {type } = this.props;
    if (type === "Create") {
      this.createUniverse();
    } else if (type === "Async") {
      const {universe: {currentUniverse: {data: {universeDetails}}}} = this.props;
      const readOnlyCluster = universeDetails && getReadOnlyCluster(universeDetails.clusters);
      if (isNonEmptyObject(readOnlyCluster)) {
        this.editReadReplica();
      } else {
        this.addReadReplica();
      }
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

  addReadReplica = () => {
    const {universe: {currentUniverse: {data: {universeUUID}}}} = this.props;
    this.props.submitAddUniverseReadReplica(this.getFormPayload(), universeUUID);
  }

  editReadReplica = () => {
    const {universe: {currentUniverse: {data: {universeUUID}}}} = this.props;
    this.props.submitEditUniverseReadReplica(this.getFormPayload(), universeUUID);
  }

  componentWillMount() {
    this.props.resetConfig();
    this.setState({editNotAllowed: true});
  }

  componentWillUpdate(newProps) {
    if (newProps.universe.formSubmitSuccess) {
      this.props.reset();
    }
  }

  // For Async clusters, we need to fetch the universe name from the
  // primary cluster metadata
  getUniverseName = () => {
    const {formValues, universe} = this.props;

    if (isNonEmptyObject(formValues['primary'])) {
      return formValues['primary'].universeName;
    }

    const {currentUniverse: {data: {universeDetails}}} = universe;
    if (isNonEmptyObject(universeDetails)) {
      const primaryCluster = getPrimaryCluster(universeDetails.clusters);
      return primaryCluster.userIntent.universeName;
    }
    // We shouldn't get here!!!
    return null;
  }

  getFormPayload = () => {
    const {formValues, universe, type} = this.props;

    const {universeConfigTemplate, currentUniverse: {data: {universeDetails}}} = universe;
    const submitPayload = _.clone(universeConfigTemplate.data, true);
    const self = this;
    const getIntentValues = function (clusterType) {
      if (!isNonEmptyObject(formValues[clusterType]) ||
          !isNonEmptyString(formValues[clusterType].provider) ||
          !isNonEmptyArray(formValues[clusterType].regionList)) {
        return null;
      }
      const clusterIntent = {
        regionList: formValues[clusterType].regionList.map(function (item) {
          return item.value;
        }),
        // We only have universe name field captured at primary form
        universeName: self.getUniverseName().trim(),
        provider: formValues[clusterType].provider,
        assignPublicIP: formValues[clusterType].assignPublicIP,
        useTimeSync: formValues[clusterType].useTimeSync,
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
          ebsType: formValues[clusterType].ebsType,
          storageClass: formValues[clusterType].storageClass
        },
        spotPrice: normalizeToPositiveFloat(formValues[clusterType].spotPrice)
      };
      if (clusterType === "primary") {
        clusterIntent.masterGFlags = formValues.primary.masterGFlags.filter((masterFlag) => {
          return isNonEmptyString(masterFlag.name) && isNonEmptyString(masterFlag.value);
        }).map((masterFlag) => {
          return {name: masterFlag.name, value: masterFlag.value};
        });
        clusterIntent.tserverGFlags = formValues.primary.tserverGFlags.filter((tserverFlag) => {
          return isNonEmptyString(tserverFlag.name) && isNonEmptyString(tserverFlag.value);
        }).map((tserverFlag) => {
          return {name: tserverFlag.name, value: tserverFlag.value.trim()};
        });
      } else {
        if (isDefinedNotNull(formValues.primary)) {
          clusterIntent.tserverGFlags = formValues.primary.tserverGFlags.filter((tserverFlag) => {
            return isNonEmptyString(tserverFlag.name) && isNonEmptyString(tserverFlag.value);
          }).map((tserverFlag) => {
            return {name: tserverFlag.name, value: tserverFlag.value};
          });
        } else {
          const existingTserverGFlags = getPrimaryCluster(universeDetails.clusters).userIntent.tserverGFlags;
          const tserverGFlags = [];
          Object.entries(existingTserverGFlags).forEach(
            ([key, value]) => tserverGFlags.push({'name': key, 'value': value.trim()})
          );
          clusterIntent.tserverGFlags = tserverGFlags;
        }
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
    submitPayload.clusters = submitPayload.clusters.filter((c)=>(c.userIntent !== null));
    // filter clusters array if configuring(adding only) Read Replica due to server side validation
    if (type === "Async") {
      submitPayload.clusters = submitPayload.clusters.filter((c)=>(c.clusterType !== "PRIMARY"));
      if (!isDefinedNotNull(getReadOnlyCluster(universeDetails.clusters))) {
        submitPayload.nodeDetailsSet = submitPayload.nodeDetailsSet.filter((c)=>(c.state === "ToBeAdded"));
      }
    }
    return submitPayload;
  }

  render() {
    const {handleSubmit, universe, softwareVersions, cloud,  getInstanceTypeListItems, submitConfigureUniverse, type,
      getRegionListItems, resetConfig, formValues, getSuggestedSpotPrice, fetchUniverseResources, fetchNodeInstanceList,
      resetSuggestedSpotPrice, showDeleteReadReplicaModal, closeModal} = this.props;
    const createUniverseTitle =
      (<h2 className="content-title">
        <FlexContainer>
          <FlexShrink>
            <div>{this.props.type} universe</div>
          </FlexShrink>
          <FlexShrink className={this.state.currentView === "Primary" ? 'stepper-cell active-stepper-cell' : 'stepper-cell'}>
            1. Primary Cluster
          </FlexShrink>
          <FlexShrink className={this.state.currentView === "Primary" ? 'stepper-cell' : 'stepper-cell active-stepper-cell'}>
            2. Read Replica (Beta)
          </FlexShrink>
        </FlexContainer>
      </h2>);

    let primaryUniverseName = "";
    if (universe && isDefinedNotNull(universe.currentUniverse.data.universeDetails)) {
      primaryUniverseName = getPrimaryCluster(universe.currentUniverse.data.universeDetails.clusters).userIntent.universeName;
    }

    const pageTitle = (({type}) => {
      if (type === "Async") {
        return <h2 className="content-title">{primaryUniverseName}<span> <i className="fa fa-chevron-right"></i> Configure read replica </span></h2>;
      } else {
        if (type === "Create") {
          return createUniverseTitle;
        } else {
          return <h2 className="content-title">{primaryUniverseName}<span> <i className="fa fa-chevron-right"></i> {this.props.type} Universe </span></h2>;
        }
      }
    })(this.props);

    let clusterForm = <span/>;
    let primaryReplicaBtn = <span/>;
    let asyncReplicaBtn = <span/>;

    if (this.state.currentView === "Async" && type !== "Edit" && type !== "Async") {
      primaryReplicaBtn = <YBButton btnClass="btn btn-default universe-form-submit-btn" btnText={"Previous"} onClick={this.configurePrimaryCluster}/>;
    }

    if (this.state.currentView === "Primary" && type !== "Edit" && type !== "Async") {
      asyncReplicaBtn = <YBButton btnClass="btn btn-default universe-form-submit-btn" btnText={"Configure Read Replica (Beta)"} onClick={this.configureReadOnlyCluster}/>;
    }

    const {universe: {currentUniverse: {data: {universeDetails}}}, modal } = this.props;
    const readOnlyCluster = universeDetails && getReadOnlyCluster(universeDetails.clusters);

    if (type === "Async") {
      if(readOnlyCluster) {
        asyncReplicaBtn = <YBButton btnClass="btn btn-default universe-form-submit-btn" btnText={"Delete this configuration"} onClick={showDeleteReadReplicaModal}/>;
      } else {
        //asyncReplicaBtn = <YBButton btnClass="btn btn-orange universe-form-submit-btn" btnText={"Add Read Replica (Beta)"} onClick={this.configureReadOnlyCluster}/>;
      }
    }
    let submitTextLabel = "";
    if (type === "Create") {
      submitTextLabel = "Create";
    } else {
      if (type === "Async") {
        if(readOnlyCluster) {
          submitTextLabel = "Edit Read Replica (Beta)" ;
        } else {
          submitTextLabel = "Add Read Replica (Beta)";
        }
      } else {
        submitTextLabel = "Save";
      }
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
      fetchCurrentUniverse: this.props.fetchCurrentUniverse, location: this.props.location,
    };

    if (this.state.currentView === "Primary") {
      clusterForm = (<PrimaryClusterFields {...clusterProps}/>);
    } else {
      // show async cluster if view if async
      clusterForm = (<ReadOnlyClusterFields {...clusterProps} />);
    }

    return (
      <Grid id="page-wrapper" fluid={true} className="universe-form-new">
        <DeleteUniverseContainer visible={modal.showModal && modal.visibleModal==="deleteReadReplicaModal"}
                               onHide={closeModal} title="Delete Read Replica of " body="Are you sure you want to delete this read replica cluster?" type="async"/>
        {pageTitle}
        <form name="UniverseForm" className="universe-form-container" onSubmit={handleSubmit(this.handleSubmitButtonClick)}>
          {clusterForm}
          <div className="form-action-button-container">
            <UniverseResources resources={universe.universeResourceTemplate.data}>
              {primaryReplicaBtn}
              <YBButton btnClass="btn btn-default universe-form-submit-btn" btnText="Cancel" onClick={this.handleCancelButtonClick}/>
              {asyncReplicaBtn}
              <YBButton btnClass="btn btn-orange universe-form-submit-btn" btnText={submitTextLabel} btnType={"submit"} />
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
        'primary.assignPublicIP', 'primary.useTimeSync', 'primary.storageClass']} component={ClusterFields} {...this.props} clusterType={"primary"} />
    );
  }
}

class ReadOnlyClusterFields extends Component {
  render() {
    return (
      <Fields names={['primary.universeName', 'async.provider', 'async.providerType', 'async.regionList', 'async.replicationFactor',
        'async.numNodes', 'async.instanceType', 'async.ybSoftwareVersion', 'async.diskIops',
        'async.numVolumes','async.volumeSize', 'async.spotPrice', 'async.useSpotPrice',
        'async.ebsType', 'async.assignPublicIP', 'async.useTimeSync', 'async.storageClass']}
              component={ClusterFields} {...this.props} clusterType={"async"}/>
    );
  }
}

export default withRouter(UniverseForm);

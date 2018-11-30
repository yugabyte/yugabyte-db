// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { Field, FieldArray } from 'redux-form';
import {browserHistory} from 'react-router';
import _ from 'lodash';
import { isDefinedNotNull, isNonEmptyObject, isNonEmptyString, areIntentsEqual, isEmptyObject,
         isNonEmptyArray, normalizeToPositiveFloat, trimSpecialChars } from 'utils/ObjectUtils';
import { YBTextInput, YBTextInputWithLabel, YBSelectWithLabel, YBMultiSelectWithLabel, YBRadioButtonBarWithLabel,
         YBToggle, YBUnControlledNumericInput, YBControlledNumericInputWithLabel } from 'components/common/forms/fields';
import {getPromiseState} from 'utils/PromiseUtils';
import AZSelectorTable from './AZSelectorTable';
import './UniverseForm.scss';
import AZPlacementInfo from './AZPlacementInfo';
import GFlagArrayComponent from './GFlagArrayComponent';
import { IN_DEVELOPMENT_MODE } from '../../../config';
import { getPrimaryCluster, getReadOnlyCluster, getClusterByType } from "../../../utils/UniverseUtils";

// Default instance types for each cloud provider
const DEFAULT_INSTANCE_TYPE_MAP = {
  'aws': 'c4.2xlarge',
  'gcp': 'n1-standard-1',
  'kubernetes': 'small'
};


const initialState = {
  universeName: '',
  instanceTypeSelected: '',
  azCheckState: true,
  providerSelected: '',
  regionList: [],
  numNodes: 3,
  nodeSetViaAZList: false,
  replicationFactor: 3,
  deviceInfo: {},
  placementInfo: {},
  ybSoftwareVersion: '',
  gflags: {},
  ebsType: 'GP2',
  accessKeyCode: 'yugabyte-default',
  // Maximum Number of nodes currently in use OnPrem case
  maxNumNodes: -1,
  // Do not use spot price anywhere, by default.
  useSpotPrice: IN_DEVELOPMENT_MODE,
  spotPrice: normalizeToPositiveFloat('0.00'),
  assignPublicIP: true,
  useTimeSync: false,
  gettingSuggestedSpotPrice: false,
  storageClass: ''
};


export default class ClusterFields extends Component {

  constructor(props) {
    super(props);
    this.providerChanged = this.providerChanged.bind(this);
    this.numNodesChanged = this.numNodesChanged.bind(this);
    this.instanceTypeChanged = this.instanceTypeChanged.bind(this);
    this.regionListChanged = this.regionListChanged.bind(this);
    this.getCurrentProvider = this.getCurrentProvider.bind(this);
    this.configureUniverseNodeList = this.configureUniverseNodeList.bind(this);
    this.handleUniverseConfigure = this.handleUniverseConfigure.bind(this);
    this.getSuggestedSpotPrice = this.getSuggestedSpotPrice.bind(this);
    this.ebsTypeChanged = this.ebsTypeChanged.bind(this);
    this.numVolumesChanged = this.numVolumesChanged.bind(this);
    this.volumeSizeChanged = this.volumeSizeChanged.bind(this);
    this.diskIopsChanged = this.diskIopsChanged.bind(this);
    this.setDeviceInfo = this.setDeviceInfo.bind(this);
    this.toggleSpotPrice = this.toggleSpotPrice.bind(this);
    this.toggleAssignPublicIP = this.toggleAssignPublicIP.bind(this);
    this.toggleUseTimeSync = this.toggleUseTimeSync.bind(this);
    this.numNodesChangedViaAzList = this.numNodesChangedViaAzList.bind(this);
    this.replicationFactorChanged = this.replicationFactorChanged.bind(this);
    this.softwareVersionChanged = this.softwareVersionChanged.bind(this);
    this.accessKeyChanged = this.accessKeyChanged.bind(this);
    this.hasFieldChanged = this.hasFieldChanged.bind(this);
    this.getCurrentUserIntent = this.getCurrentUserIntent.bind(this);

    if (this.props.type === "Async" && isNonEmptyObject(this.props.universe.currentUniverse.data)) {
      if (isDefinedNotNull(getReadOnlyCluster(this.props.universe.currentUniverse.data.universeDetails.clusters))) {
        this.state = { ...initialState, isReadOnlyExists: true, editNotAllowed: this.props.editNotAllowed};
      } else {
        this.state = { ...initialState, isReadOnlyExists: false, editNotAllowed: false};
      }
    } else {
      this.state = initialState;
    }
  }

  componentWillMount() {
    const {formValues, clusterType, updateFormField, type} = this.props;
    const {universe: {currentUniverse: {data: {universeDetails}}}} = this.props;
    // Set default software version in case of create
    if (isNonEmptyArray(this.props.softwareVersions) && !isNonEmptyString(this.state.ybSoftwareVersion) && type === "Create") {
      this.setState({ybSoftwareVersion: this.props.softwareVersions[0]});
      updateFormField(`${clusterType}.ybSoftwareVersion`, this.props.softwareVersions[0]);
    }

    if (isNonEmptyObject(formValues['primary']) && clusterType !== 'primary') {
      this.setState({universeName: formValues['primary'].universeName});
      updateFormField(`${clusterType}.universeName`, formValues['primary'].universeName);
    }

    // This flag will prevent configure from being fired on component load
    if (formValues && isNonEmptyObject(formValues[clusterType])) {
      this.setState({nodeSetViaAZList: true});
    }

    const isEditReadOnlyFlow = type === "Async" && this.state.isReadOnlyExists;
    if (type === "Edit" || isEditReadOnlyFlow) {
      const primaryCluster = getPrimaryCluster(universeDetails.clusters);
      const readOnlyCluster = getReadOnlyCluster(universeDetails.clusters);
      const userIntent = clusterType === "async" ? readOnlyCluster && { ...readOnlyCluster.userIntent, universeName: primaryCluster.userIntent.universeName } : primaryCluster && primaryCluster.userIntent;
      const providerUUID = userIntent && userIntent.provider;
      if (userIntent && providerUUID) {
        const ebsType = (userIntent.deviceInfo === null) ? null : userIntent.deviceInfo.ebsType;
        this.setState({
          providerSelected: providerUUID,
          instanceTypeSelected: userIntent.instanceType,
          numNodes: userIntent.numNodes,
          replicationFactor: userIntent.replicationFactor,
          ybSoftwareVersion: userIntent.ybSoftwareVersion,
          accessKeyCode: userIntent.accessKeyCode,
          deviceInfo: userIntent.deviceInfo,
          storageClass: userIntent.deviceInfo.storageClass,
          ebsType: ebsType,
          regionList: userIntent.regionList,
          volumeType: (ebsType === null) ? "SSD" : "EBS",
          useSpotPrice: parseFloat(userIntent.spotPrice) > 0.0,
          spotPrice: userIntent.spotPrice
        });
      }

      this.props.getRegionListItems(providerUUID);
      this.props.getInstanceTypeListItems(providerUUID);
      if (primaryCluster.userIntent.providerType === "onprem") {
        this.props.fetchNodeInstanceList(providerUUID);
      }
      // If Edit Case Set Initial Configuration
      this.props.getExistingUniverseConfiguration(universeDetails);
    } else {
      // Repopulate the form fields when switching back to the view
      if (formValues && isNonEmptyObject(formValues[clusterType])) {
        this.setState({providerSelected: formValues[clusterType].provider});
        this.setState({numNodes: formValues[clusterType].numNodes ? formValues[clusterType].numNodes : 3});
        this.setState({replicationFactor: formValues[clusterType].replicationFactor ?
                                          Number(formValues[clusterType].replicationFactor) : 3});
        if (isNonEmptyString(formValues[clusterType].provider)) {
          this.props.getInstanceTypeListItems(formValues[clusterType].provider);
          this.props.getRegionListItems(formValues[clusterType].provider);
          this.setState({instanceTypeSelected: formValues[clusterType].instanceType});

          if (formValues[clusterType].spotPrice && formValues[clusterType].spotPrice > 0) {
            this.setState({useSpotPrice: true, spotPrice: formValues[clusterType].spotPrice});
          } else {
            this.setState({useSpotPrice: false, spotPrice: 0.0});
          }
          if (formValues[clusterType].assignPublicIP) {
            // We would also default to whatever primary cluster's state for this one.
            this.setState({assignPublicIP: formValues['primary'].assignPublicIP});
          }
          if (formValues[clusterType].useTimeSync) {
            // We would also default to whatever primary cluster's state for this one.
            this.setState({useTimeSync: formValues['primary'].useTimeSync});
          }
        }
      } else {
        // Initialize the form values if not exists
        updateFormField(`${clusterType}.numNodes`, 3);
        updateFormField(`${clusterType}.replicationFactor`, 3);
      }
    }
  }

  componentWillReceiveProps(nextProps) {
    const {universe: {currentUniverse}, cloud: {nodeInstanceList, instanceTypes, suggestedSpotPrice}, clusterType, formValues, updateFormField} = nextProps;

    const currentFormValues = formValues[clusterType];
    let providerSelected = this.state.providerSelected;
    if (isNonEmptyObject(currentFormValues) && isNonEmptyString(currentFormValues.provider)) {
      providerSelected = currentFormValues.provider;
    }

    if (nextProps.cloud.instanceTypes.data !== this.props.cloud.instanceTypes.data
      && isNonEmptyArray(nextProps.cloud.instanceTypes.data) && providerSelected) {

      if (nextProps.type === "Create" || (nextProps.type === "Async" && !this.state.isReadOnlyExists)) {
        let instanceTypeSelected = null;
        const currentProviderCode = this.getCurrentProvider(providerSelected).code;
        instanceTypeSelected = DEFAULT_INSTANCE_TYPE_MAP[currentProviderCode];
        // If we have the default instance type in the cloud instance types then we
        // use it, otherwise we pick the first one in the list and use it.
        const hasInstanceType = instanceTypes.data.find((it) => {
          return it.providerCode === currentProviderCode && it.instanceTypeCode === instanceTypeSelected;
        });
        if (!hasInstanceType) {
          instanceTypeSelected = instanceTypes.data[0].instanceTypeCode;
        }

        const instanceTypeSelectedData = instanceTypes.data.find(function (item) {
          return item.instanceTypeCode === formValues[clusterType].instanceType;
        });

        if (isNonEmptyObject(instanceTypeSelectedData)) {
          instanceTypeSelected = formValues[clusterType].instanceType;
        }

        this.props.updateFormField(`${clusterType}.instanceType`, instanceTypeSelected);
        this.setState({instanceTypeSelected: instanceTypeSelected});
        this.setDeviceInfo(instanceTypeSelected, instanceTypes.data);
      };
    }

    // Set default ebsType once API call has completed
    if (isNonEmptyArray(nextProps.cloud.ebsTypes) && !isNonEmptyArray(this.props.cloud.ebsTypes)) {
      this.props.updateFormField(`${clusterType}.ebsType`, 'GP2');
      this.setState({"ebsType": "GP2"});
    }

    if (isNonEmptyArray(nextProps.softwareVersions) && isNonEmptyObject(this.props.formValues[clusterType]) && !isNonEmptyString(this.props.formValues[clusterType].ybSoftwareVersion)) {
      this.setState({ybSoftwareVersion: nextProps.softwareVersions[0]});
      this.props.updateFormField(`${clusterType}.ybSoftwareVersion`, nextProps.softwareVersions[0]);
    }

    if (isNonEmptyArray(this.getStorageClasses()) && !isDefinedNotNull(this.state.storageClass)) {
      this.setState({storageClass: this.getStorageClasses()[0]});
      this.props.updateFormField(`${clusterType}.storageClass`, this.getStorageClasses()[0]);
    }

    // Set spot price
    const currentPromiseState = getPromiseState(this.props.cloud.suggestedSpotPrice);
    const nextPromiseState = getPromiseState(suggestedSpotPrice);
    if (currentPromiseState.isLoading()) {
      if (nextPromiseState.isSuccess()) {
        this.setState({
          spotPrice: normalizeToPositiveFloat(suggestedSpotPrice.data.toString()),
          useSpotPrice: true,
          gettingSuggestedSpotPrice: false
        });
        updateFormField(`${clusterType}.spotPrice`, normalizeToPositiveFloat(suggestedSpotPrice.data.toString()));
        updateFormField(`${clusterType}.useSpotPrice`, true);
      } else if (nextPromiseState.isError()) {
        this.setState({
          spotPrice: normalizeToPositiveFloat('0.00'),
          useSpotPrice: false,
          gettingSuggestedSpotPrice: false
        });
        updateFormField(`${clusterType}.spotPrice`, normalizeToPositiveFloat('0.00'));
        updateFormField(`${clusterType}.useSpotPrice`, false);
      }
    }

    // Form Actions on Create Universe Success
    if (getPromiseState(this.props.universe.createUniverse).isLoading() && getPromiseState(nextProps.universe.createUniverse).isSuccess()) {
      this.props.reset();
      this.props.fetchUniverseMetadata();
      this.props.fetchCustomerTasks();
      if (this.context.prevPath) {
        browserHistory.push(this.context.prevPath);
      } else {
        browserHistory.push("/universes");
      }
    }
    // Form Actions on Edit Universe Success
    if ((getPromiseState(this.props.universe.editUniverse).isLoading() && getPromiseState(nextProps.universe.editUniverse).isSuccess()) ||
        (getPromiseState(this.props.universe.addReadReplica).isLoading() && getPromiseState(nextProps.universe.addReadReplica).isSuccess()) ||
        (getPromiseState(this.props.universe.editReadReplica).isLoading() && getPromiseState(nextProps.universe.editReadReplica).isSuccess()) ||
        (getPromiseState(this.props.universe.deleteReadReplica).isLoading() && getPromiseState(nextProps.universe.deleteReadReplica).isSuccess())) {
      this.props.fetchCurrentUniverse(currentUniverse.data.universeUUID);
      this.props.fetchUniverseMetadata();
      this.props.fetchCustomerTasks();
      this.props.fetchUniverseTasks(currentUniverse.data.universeUUID);
      browserHistory.push(this.props.location.pathname);
    }
    // Form Actions on Configure Universe Success
    if (getPromiseState(this.props.universe.universeConfigTemplate).isLoading() && getPromiseState(nextProps.universe.universeConfigTemplate).isSuccess()) {
      this.props.fetchUniverseResources(nextProps.universe.universeConfigTemplate.data);
    }
    // If nodeInstanceList changes, fetch number of available nodes
    if (getPromiseState(nodeInstanceList).isSuccess() && getPromiseState(this.props.cloud.nodeInstanceList).isLoading()) {
      let numNodesAvailable = nodeInstanceList.data.reduce(function (acc, val) {
        if (!val.inUse) {
          acc++;
        }
        return acc;
      }, 0);
      // Add Existing nodes in Universe userIntent to available nodes for calculation in case of Edit
      if (this.props.type === "Edit" || (nextProps.type === "Async" && !this.state.isReadOnlyExists)) {
        const cluster = getClusterByType(currentUniverse.data.universeDetails.clusters, clusterType);
        if (isDefinedNotNull(cluster)) {
          numNodesAvailable += cluster.userIntent.numNodes;
        }
      }
      this.setState({maxNumNodes: numNodesAvailable});
    }
  }

  componentDidUpdate(prevProps, prevState) {
    const {universe: {currentUniverse}, formValues, clusterType} = this.props;
    let currentProviderUUID = this.state.providerSelected;
    const self = this;

    if (isNonEmptyObject(formValues[clusterType]) && isNonEmptyString(formValues[clusterType].provider)) {
      currentProviderUUID = formValues[clusterType].provider;
    }
    const currentProvider = this.getCurrentProvider(currentProviderUUID);
    const hasSpotPriceChanged = function() {
      if (formValues[clusterType] && prevProps.formValues[clusterType]) {
        return formValues[clusterType].spotPrice !== prevProps.formValues[clusterType].spotPrice;
      } else {
        return false;
      }
    };

    const configureIntentValid = function() {
      return (!_.isEqual(self.state, prevState) || hasSpotPriceChanged()) &&
        isNonEmptyObject(currentProvider) &&
        isNonEmptyArray(formValues[clusterType].regionList) &&
        (prevState.maxNumNodes !== -1 || currentProvider.code !== "onprem") &&

        (currentProvider.code !== "aws" || (!self.state.gettingSuggestedSpotPrice && (!self.state.useSpotPrice ||
        (self.state.useSpotPrice && formValues[clusterType].spotPrice > 0))))
        &&
        ((currentProvider.code === "onprem" &&
        self.state.numNodes <= self.state.maxNumNodes) || (currentProvider.code !== "onprem")) &&
        self.state.numNodes >= self.state.replicationFactor &&
        !self.state.nodeSetViaAZList;
    };

    // Fire Configure only if either provider is not on-prem or maxNumNodes is not -1 if on-prem
    if (configureIntentValid()) {
      if (isNonEmptyObject(currentUniverse.data)) {
        if (this.hasFieldChanged()) {
          this.configureUniverseNodeList();
        } else {
          const placementStatusObject = {
            error: {
              type: "noFieldsChanged",
              numNodes: this.state.numNodes,
              maxNumNodes: this.state.maxNumNodes
            }
          };
          this.props.setPlacementStatus(placementStatusObject);
        }
      } else {
        this.configureUniverseNodeList();
      }
    } else if (isNonEmptyArray(this.state.regionList) && currentProvider &&
      currentProvider.code === "onprem" && this.state.instanceTypeSelected &&
      this.state.numNodes > this.state.maxNumNodes) {

      const placementStatusObject = {
        error: {
          type: "notEnoughNodesConfigured",
          numNodes: this.state.numNodes,
          maxNumNodes: this.state.maxNumNodes
        }
      };
      this.props.setPlacementStatus(placementStatusObject);
    }
  }


  numNodesChangedViaAzList(value) {
    const {updateFormField, clusterType} = this.props;
    this.setState({nodeSetViaAZList: true, numNodes: value});
    updateFormField(`${clusterType}.numNodes`, value);
  }

  setDeviceInfo(instanceTypeCode, instanceTypeList) {
    const {updateFormField, clusterType} = this.props;
    const instanceTypeSelectedData = instanceTypeList.find(function (item) {
      return item.instanceTypeCode === instanceTypeCode;
    });
    const volumesList = instanceTypeSelectedData.instanceTypeDetails.volumeDetailsList;
    const volumeDetail = volumesList[0];
    let mountPoints = null;
    if (instanceTypeSelectedData.providerCode === "onprem") {
      mountPoints = instanceTypeSelectedData.instanceTypeDetails.volumeDetailsList.map(function (item) {
        return item.mountPath;
      }).join(",");
    }
    if (volumeDetail) {
      const deviceInfo = {
        volumeSize: volumeDetail.volumeSizeGB,
        numVolumes: volumesList.length,
        mountPoints: mountPoints,
        ebsType: volumeDetail.volumeType === "EBS" ? "GP2" : null,
        diskIops: null
      };
      updateFormField(`${clusterType}.volumeSize`, volumeDetail.volumeSizeGB);
      updateFormField(`${clusterType}.numVolumes`, volumesList.length);
      updateFormField(`${clusterType}.diskIops`, volumeDetail.diskIops);
      updateFormField(`${clusterType}.ebsType`, volumeDetail.ebsType);
      updateFormField(`${clusterType}.mountPoints`, mountPoints);
      this.setState({deviceInfo: deviceInfo, volumeType: volumeDetail.volumeType});
    }
  }

  getCurrentUserIntent = () => {
    const {formValues, clusterType, universe: { currentUniverse } } = this.props;
    const primaryCluster = getPrimaryCluster(currentUniverse.data.universeDetails.clusters);
    if (formValues[clusterType]) {
      return {
        universeName: primaryCluster.userIntent.universeName,
        numNodes: formValues[clusterType].numNodes,
        provider: formValues[clusterType].provider,
        providerType: this.getCurrentProvider(formValues[clusterType].provider).code,
        regionList: formValues[clusterType].regionList.map((a)=>(a.value)),
        instanceType: formValues[clusterType].instanceType,
        ybSoftwareVersion: formValues[clusterType].ybSoftwareVersion,
        replicationFactor: formValues[clusterType].replicationFactor,
        deviceInfo: {
          volumeSize: formValues[clusterType].volumeSize,
          numVolumes: formValues[clusterType].numVolumes,
          diskIops: formValues[clusterType].diskIops,
          mountPoints: formValues[clusterType].mountPoints,
          ebsTypes: formValues[clusterType].ebsTypes
        },
        accessKeyCode: formValues[clusterType].accessKeyCode,
        gflags: formValues[clusterType].gflags,
        spotPrice: formValues[clusterType].spotPrice,
        useTimeSync: formValues[clusterType].useTimeSync,
        storageClass: formValues[clusterType].storageClass
      };
    }
  };

  softwareVersionChanged(value) {
    const {updateFormField, clusterType} = this.props;
    this.setState({ybSoftwareVersion: value});
    updateFormField(`${clusterType}.ybSoftwareVersion`, value);
  }

  ebsTypeChanged(ebsValue) {
    const {updateFormField, clusterType} = this.props;
    const currentDeviceInfo = _.clone(this.state.deviceInfo);
    currentDeviceInfo.ebsType = ebsValue;
    if (currentDeviceInfo.ebsType === "IO1" && currentDeviceInfo.diskIops == null) {
      currentDeviceInfo.diskIops = 1000;
      updateFormField(`${clusterType}.diskIops`, 1000);
    } else {
      currentDeviceInfo.diskIops = null;
    }
    updateFormField(`${clusterType}.ebsType`, ebsValue);
    this.setState({deviceInfo: currentDeviceInfo, ebsType: ebsValue});
  }

  numVolumesChanged(val) {
    const {updateFormField, clusterType} = this.props;
    updateFormField(`${clusterType}.numVolumes`, val);
    this.setState({deviceInfo: {...this.state.deviceInfo, numVolumes: val}});
  }

  volumeSizeChanged(val) {
    const {updateFormField, clusterType} = this.props;
    updateFormField(`${clusterType}.volumeSize`, val);
    this.setState({deviceInfo: {...this.state.deviceInfo, volumeSize: val}});
  }

  diskIopsChanged(val) {
    const {updateFormField, clusterType} = this.props;
    updateFormField(`${clusterType}.diskIops`, val);
    if (this.state.deviceInfo.ebsType === "IO1") {
      this.setState({deviceInfo: {...this.state.deviceInfo, diskIops: val}});
    }
  }

  toggleSpotPrice(event) {
    const {updateFormField, clusterType} = this.props;
    const nextState = {useSpotPrice: event.target.checked};
    if (event.target.checked) {
      this.getSuggestedSpotPrice(this.state.instanceTypeSelected, this.state.regionList);
    } else {
      nextState['spotPrice'] = initialState.spotPrice;
      this.props.resetSuggestedSpotPrice();
      updateFormField(`${clusterType}.spotPrice`, normalizeToPositiveFloat('0.00'));
    }
    this.setState(nextState);
    updateFormField(`${clusterType}.useSpotPrice`, event.target.checked);
  }

  toggleUseTimeSync(event) {
    const {updateFormField, clusterType} = this.props;
    updateFormField(`${clusterType}.useTimeSync`, event.target.checked);
    this.setState({useTimeSync: event.target.checked});
  }


  toggleAssignPublicIP(event) {
    const {updateFormField, clusterType} = this.props;
    // Right now we only let primary cluster to update this flag, and
    // keep the async cluster to use the same value as primary.
    if (clusterType === "primary") {
      updateFormField('primary.assignPublicIP', event.target.checked);
      updateFormField('async.assignPublicIP', event.target.checked);
      this.setState({assignPublicIP: event.target.checked});
    }
  }

  spotPriceChanged(val, normalize) {
    const {updateFormField, clusterType} = this.props;
    this.setState({spotPrice: normalize ? normalizeToPositiveFloat(val) : val});
    if (normalize) {
      this.setState({spotPrice: normalizeToPositiveFloat(val)});
      updateFormField(`${clusterType}.spotPrice`, normalizeToPositiveFloat(val));
    } else {
      this.setState({spotPrice: val});
      updateFormField(`${clusterType}.spotPrice`, val);
    }
  }

  replicationFactorChanged = value => {
    const {updateFormField, clusterType, universe: {currentUniverse: {data}}} = this.props;
    const clusterExists = isDefinedNotNull(data.universeDetails) ? isEmptyObject(getClusterByType(data.universeDetails.clusters, clusterType)) : null;
    const self = this;

    if (!clusterExists) {
      this.setState({nodeSetViaAZList: false, replicationFactor: value}, function () {
        if (self.state.numNodes <= value) {
          self.setState({numNodes: value});
          updateFormField(`${clusterType}.numNodes`, value);
        }
      });
    }
    updateFormField(`${clusterType}.replicationFactor`, value);
  };

  hasFieldChanged = () => {
    const {universe: {currentUniverse}} = this.props;
    if (isEmptyObject(currentUniverse.data) || isEmptyObject(currentUniverse.data.universeDetails)) {
      return true;
    }
    const primaryCluster = getPrimaryCluster(currentUniverse.data.universeDetails.clusters);
    const existingIntent = isNonEmptyObject(primaryCluster) ?
      _.clone(primaryCluster.userIntent, true) : null;
    const currentIntent = this.getCurrentUserIntent();

    return !areIntentsEqual(existingIntent, currentIntent);
  };

  handleUniverseConfigure(universeTaskParams) {
    const {universe: {universeConfigTemplate, currentUniverse}, formValues, clusterType} = this.props;

    const instanceType = formValues[clusterType].instanceType;
    const regionList = formValues[clusterType].regionList;
    const verifyIntentConditions = function() {
      return isNonEmptyArray(regionList) && isNonEmptyString(instanceType);
    };

    if (verifyIntentConditions() ) {
      if (isNonEmptyObject(currentUniverse.data) && isNonEmptyObject(currentUniverse.data.universeDetails) && isDefinedNotNull(getClusterByType(currentUniverse.data.universeDetails.clusters, clusterType))) {
        // cluster set: main edit flow
        const oldCluster = getClusterByType(currentUniverse.data.universeDetails.clusters, clusterType);
        const newCluster = getClusterByType(universeTaskParams.clusters, clusterType);
        if (isNonEmptyObject(oldCluster) && isNonEmptyObject(newCluster) &&
          areIntentsEqual(oldCluster.userIntent, newCluster.userIntent)) {
          this.props.getExistingUniverseConfiguration(currentUniverse.data.universeDetails);
        } else {
          this.props.submitConfigureUniverse(universeTaskParams);
        }
      } else {
        // Create flow
        if (isEmptyObject(universeConfigTemplate.data)) {
          this.props.submitConfigureUniverse(universeTaskParams);
        } else {
          const currentClusterConfiguration = getClusterByType(universeConfigTemplate.data.clusters, clusterType);
          if(!isDefinedNotNull(currentClusterConfiguration)) {
            this.props.submitConfigureUniverse(universeTaskParams);
          } else if (!areIntentsEqual(
                      getClusterByType(universeTaskParams.clusters, clusterType).userIntent,
                      currentClusterConfiguration.userIntent) ) {
            this.props.submitConfigureUniverse(universeTaskParams);
          }
        }
      }
    }
  }

  updateTaskParams = (universeTaskParams, userIntent, clusterType) => {
    const cluster = getClusterByType(universeTaskParams.clusters, clusterType);
    universeTaskParams.currentClusterType = clusterType.toUpperCase();
    const isEdit = this.props.type === "Edit" ||
      (this.props.type === "Async" && this.state.isReadOnlyExists);

    if (isDefinedNotNull(cluster)) {
      cluster.userIntent = userIntent;
    } else {
      if (isEmptyObject(universeTaskParams.clusters)) {
        universeTaskParams.clusters = [];
      }
      universeTaskParams.clusters.push({
        clusterType: clusterType.toUpperCase(),
        userIntent: userIntent
      });
    }
    universeTaskParams.clusterOperation = isEdit ? "EDIT": "CREATE";
  }

  configureUniverseNodeList() {
    const {universe: {universeConfigTemplate, currentUniverse}, formValues, clusterType} = this.props;

    let universeTaskParams = {};
    if (isNonEmptyObject(universeConfigTemplate.data)) {
      universeTaskParams = _.cloneDeep(universeConfigTemplate.data);
    }
    if (this.props.type === "Async" && !isDefinedNotNull(getReadOnlyCluster(currentUniverse.data.universeDetails.clusters))) {
      universeTaskParams = _.cloneDeep(currentUniverse.data.universeDetails);
    }
    if (isNonEmptyObject(currentUniverse.data)) {
      universeTaskParams.universeUUID = currentUniverse.data.universeUUID;
      universeTaskParams.expectedUniverseVersion = currentUniverse.data.version;
    }

    const userIntent = {
      universeName: formValues[clusterType].universeName,
      provider: formValues[clusterType].provider,
      regionList: formValues[clusterType].regionList && formValues[clusterType].regionList.map(function (item) {
        return item.value;
      }),
      assignPublicIP: formValues[clusterType].assignPublicIP,
      useTimeSync: formValues[clusterType].useTimeSync,
      numNodes: formValues[clusterType].numNodes,
      instanceType: formValues[clusterType].instanceType,
      ybSoftwareVersion: formValues[clusterType].ybSoftwareVersion,
      replicationFactor: formValues[clusterType].replicationFactor,
      deviceInfo: {
        volumeSize: formValues[clusterType].volumeSize,
        numVolumes: formValues[clusterType].numVolumes,
        mountPoints: formValues[clusterType].mountPoints,
        ebsType: formValues[clusterType].ebsType,
        diskIops: formValues[clusterType].diskIops,
        storageClass: formValues[clusterType].storageClass
      },
      accessKeyCode: formValues[clusterType].accessKeyCode,
      spotPrice: formValues[clusterType].spotPrice
    };

    if (isNonEmptyObject(formValues[clusterType].masterGFlags)) {
      userIntent["masterGFlags"] = formValues[clusterType].masterGFlags;
    }
    if (isNonEmptyObject(formValues[clusterType].tserverGFlags)) {
      userIntent["tserverGFlags"] = formValues[clusterType].tserverGFlags;
    }

    this.props.cloud.providers.data.forEach(function (providerItem) {
      if (providerItem.uuid === formValues[clusterType].provider) {
        userIntent.providerType = providerItem.code;
      }
    });

    this.updateTaskParams(universeTaskParams, userIntent, clusterType);
    universeTaskParams.userAZSelected = false;
    this.handleUniverseConfigure(universeTaskParams);
  }

  numNodesChanged(value) {
    const {updateFormField, clusterType} = this.props;
    this.setState({numNodes: value, nodeSetViaAZList: false});
    updateFormField(`${clusterType}.numNodes`, value);
  }

  getCurrentProvider(providerUUID) {
    return this.props.cloud.providers.data.find((provider) => provider.uuid === providerUUID);
  }

  providerChanged = (value) => {
    const {updateFormField, clusterType, universe: {currentUniverse: {data}}} = this.props;
    const providerUUID = value;

    const targetCluster = clusterType !== "primary" ? isNonEmptyObject(data) && getPrimaryCluster(data.universeDetails.clusters) : isNonEmptyObject(data) && getReadOnlyCluster(data.universeDetails.clusters);
    if (isEmptyObject(data) || isDefinedNotNull(targetCluster)) {
      this.props.updateFormField(`${clusterType}.regionList`, []);
      //If we have accesskeys for a current selected provider we set that in the state or we fallback to default value.
      let defaultAccessKeyCode = initialState.accessKeyCode;
      if (isNonEmptyArray(this.props.accessKeys.data)) {
        const providerAccessKeys = this.props.accessKeys.data.filter((key) => key.idKey.providerUUID === value);
        if (isNonEmptyArray(providerAccessKeys)) {
          defaultAccessKeyCode = providerAccessKeys[0].idKey.keyCode;
        }
      }
      updateFormField(`${clusterType}.accessKeyCode`, defaultAccessKeyCode);
      this.setState({nodeSetViaAZList: false, regionList: [], providerSelected: providerUUID,
        deviceInfo: {}, accessKeyCode: defaultAccessKeyCode});
      this.props.getRegionListItems(providerUUID, true);
      this.props.getInstanceTypeListItems(providerUUID);
    }
    const currentProviderData = this.getCurrentProvider(value);

    if (currentProviderData && currentProviderData.code === "onprem") {
      this.props.fetchNodeInstanceList(value);
    }
  }

  accessKeyChanged(event) {
    const {clusterType} = this.props;
    this.props.updateFormField(`${clusterType}.accessKeyCode`, event.target.value);
  }

  storageClassChanged = (value) => {
    const {clusterType} = this.props;
    this.props.updateFormField(`${clusterType}.storageClass`, value);
  }

  instanceTypeChanged(value) {
    const {updateFormField, clusterType} = this.props;
    const instanceTypeValue = value;
    updateFormField(`${clusterType}.instanceType`, instanceTypeValue);
    this.setState({instanceTypeSelected: instanceTypeValue, nodeSetViaAZList: false});

    this.setDeviceInfo(instanceTypeValue, this.props.cloud.instanceTypes.data);
    if (this.state.useSpotPrice) {
      this.getSuggestedSpotPrice(instanceTypeValue, this.state.regionList);
    } else {
      this.props.resetSuggestedSpotPrice();
    }
  }

  regionListChanged(value) {
    const {formValues, clusterType, updateFormField, cloud:{providers}} = this.props;
    this.setState({nodeSetViaAZList: false, regionList: value});
    if (this.state.useSpotPrice) {
      this.getSuggestedSpotPrice(this.state.instanceTypeSelected, value);
    } else {
      this.props.resetSuggestedSpotPrice();
    }
    const currentProvider = providers.data.find((a)=>(a.uuid === formValues[clusterType].provider));
    if (!isNonEmptyString(formValues[clusterType].instanceType)) {
      updateFormField(`${clusterType}.instanceType`, DEFAULT_INSTANCE_TYPE_MAP[currentProvider.code]);
    }
  }

  getSuggestedSpotPrice(instanceType, regions) {
    const currentProvider = this.getCurrentProvider(this.state.providerSelected);
    const regionUUIDs = regions.map(region => region.value);
    if ((this.props.type === "Create" || (this.props.clusterType === "async" && !this.state.isReadOnlyExists)) && isDefinedNotNull(currentProvider) &&
        (currentProvider.code === "aws" || currentProvider.code === "gcp") && isNonEmptyArray(regionUUIDs)) {
      this.props.getSuggestedSpotPrice(this.state.providerSelected, instanceType, regionUUIDs);
      this.setState({gettingSuggestedSpotPrice: true});
    }
  }

  getStorageClasses() {
    const currentProvider = this.getCurrentProvider(this.state.providerSelected);
    if (!isDefinedNotNull(currentProvider) || currentProvider.code !== "kubernetes") {
      return null;
    }
    if (!isDefinedNotNull(currentProvider.config.KUBECONFIG_STORAGE_CLASSES)) {
      return ['standard'];
    } else {
      return currentProvider.config.KUBECONFIG_STORAGE_CLASSES.split(",");
    }
  }

  render() {
    const {clusterType, cloud, softwareVersions, accessKeys, universe, cloud: {suggestedSpotPrice}, formValues} = this.props;
    const self = this;
    let gflagArray = <span/>;
    let universeProviderList = [];
    let currentProviderCode = "";

    let currentProviderUUID = self.state.providerSelected;
    if (formValues[clusterType] && formValues[clusterType].provider) {
      currentProviderUUID = formValues[clusterType].provider;
    }

    // Populate the cloud provider list
    if (isNonEmptyArray(cloud.providers.data)) {
      universeProviderList = cloud.providers.data.map(function(providerItem, idx) {
        if (providerItem.uuid === currentProviderUUID) {
          currentProviderCode = providerItem.code;
        }
        return (
          <option key={providerItem.uuid} value={providerItem.uuid}>
            {providerItem.name}
          </option>
        );
      });
    }

    // Spot price and EBS types
    let ebsTypeSelector = <span/>;
    let deviceDetail = null;
    let iopsField = <span/>;
    let storageClassField = <span/>;
    function volumeTypeFormat(num) {
      return num + ' GB';
    }
    const ebsTypesList =
      cloud.ebsTypes && cloud.ebsTypes.map(function (ebsType, idx) {
        return <option key={ebsType} value={ebsType}>{ebsType}</option>;
      });
    const isFieldReadOnly = isNonEmptyObject(universe.currentUniverse.data) && (this.props.type === "Edit" || (this.props.type === "Async" && this.state.isReadOnlyExists));
    const deviceInfo = this.state.deviceInfo;

    if (isNonEmptyObject(formValues[clusterType])) {
      const currentCluster = formValues[clusterType];
      if (isNonEmptyString(currentCluster.numVolumes)) {
        deviceInfo["numVolumes"] = currentCluster.numVolumes;
      }
      if (isNonEmptyString(currentCluster.volumeSize)) {
        deviceInfo["volumeSize"] = currentCluster.volumeSize;
      }
      if (isNonEmptyString(currentCluster.diskIops)) {
        deviceInfo["diskIops"] = currentCluster.diskIops;
      }
      if (isNonEmptyObject(currentCluster.ebsType)) {
        deviceInfo["ebsType"] = currentCluster.ebsType;
      }
    }

    if (isNonEmptyObject(deviceInfo)) {
      if (self.state.volumeType === 'EBS' || self.state.volumeType === 'SSD') {
        const currentProvider = this.getCurrentProvider(self.state.providerSelected);
        const isInAws = currentProvider.code === 'aws';
        // We don't want to keep the volume fixed in case of Kubernetes.
        const fixedVolumeInfo = self.state.volumeType === 'SSD' &&
                                currentProvider.code !== 'kubernetes';
        if (currentProvider.code === 'kubernetes') {
          const storageClassOptions = this.getStorageClasses().map(function(storageClassName, idx) {
            return (
              <option key={"storageClass-" + idx} value={storageClassName}>
                {storageClassName}
              </option>
            );
          });

          storageClassField = (<Field name={`${clusterType}.storageClass`} type="select"
            component={YBSelectWithLabel} label="Storage Class" readOnly={isFieldReadOnly}
            options={storageClassOptions} onInputChanged={self.storageClassChanged} />);
        }

        const isIoType = deviceInfo.ebsType === 'IO1';
        if (isIoType) {
          iopsField = (
            <span className="volume-info form-group-shrinked">
              <label className="form-item-label">Provisioned IOPS</label>
              <span className="volume-info-field volume-info-iops">
                <Field name={`${clusterType}.diskIops`} component={YBUnControlledNumericInput}
                       label="Provisioned IOPS" onInputChanged={self.diskIopsChanged}
                       readOnly={isFieldReadOnly} />
              </span>
            </span>
          );
        }
        const numVolumes = (
          <span className="volume-info-field volume-info-count">
            <Field name={`${clusterType}.numVolumes`} component={YBUnControlledNumericInput}
                   label="Number of Volumes" onInputChanged={self.numVolumesChanged}
                   readOnly={fixedVolumeInfo || isFieldReadOnly} />
          </span>
        );
        const volumeSize = (
          <span className="volume-info-field volume-info-size">
            <Field name={`${clusterType}.volumeSize`} component={YBUnControlledNumericInput}
                   label="Volume Size" valueFormat={volumeTypeFormat}
                   readOnly={fixedVolumeInfo || isFieldReadOnly} />
          </span>
        );
        deviceDetail = (
          <span className="volume-info">
            {numVolumes}
            &times;
            {volumeSize}
          </span>
        );
        // Only for AWS EBS, show type option.
        if (isInAws && self.state.volumeType === "EBS") {
          ebsTypeSelector = (
            <span className="volume-info form-group-shrinked">
              <Field name={`${clusterType}.ebsType`} component={YBSelectWithLabel}
                     options={ebsTypesList} label="EBS Type" onInputChanged={self.ebsTypeChanged}
                     readOnlySelect={isFieldReadOnly} />
            </span>
          );
        }
      }
    }

    let spotPriceToggle = <span />;
    let spotPriceField = <span />;
    let assignPublicIP = <span />;
    let useTimeSync = <span />;
    const currentProvider = this.getCurrentProvider(currentProviderUUID);

    if (isDefinedNotNull(currentProvider) &&
        (currentProvider.code === "aws" || currentProvider.code === "gcp")) {
      // Assign public ip would be only enabled for primary and that same
      // value will be used for async as well.
      const disableOnChange = clusterType !== "primary";
      assignPublicIP = (
        <Field name={`${clusterType}.assignPublicIP`}
               component={YBToggle} isReadOnly={isFieldReadOnly}
               disableOnChange={disableOnChange}
               checkedVal={this.state.assignPublicIP}
               onToggle={this.toggleAssignPublicIP}
               label="Assign Public IP"
               subLabel="Whether or not to assign a public IP."/>
      );
      if (this.state.gettingSuggestedSpotPrice) {
        spotPriceField = (
          <div className="form-group">
            <label className="form-item-label">Spot Price (Per Hour)</label>
            <div className="extra-info-field text-center">Loading suggested spot price...</div>
          </div>
        );
      } else if (!this.state.gettingSuggestedSpotPrice && this.state.useSpotPrice) {
        spotPriceField = (
          <Field name={`${clusterType}.spotPrice`} type="text"
                 component={YBTextInputWithLabel}
                 label="Spot Price (Per Hour)"
                 isReadOnly={isFieldReadOnly || !this.state.useSpotPrice || currentProvider.code === "gcp"}
                 normalizeOnBlur={(val) => this.spotPriceChanged(val, true)}
                 initValue={this.state.spotPrice.toString()}
                 onValueChanged={(val) => this.spotPriceChanged(val, false)}/>
        );
      } else if (getPromiseState(suggestedSpotPrice).isError()) {
        spotPriceField = (
          <div className="form-group">
            <label className="form-item-label">Spot Price (Per Hour)</label>
            <div className="extra-info-field text-center">Spot pricing not supported for {this.state.instanceTypeSelected} in selected regions.</div>
          </div>
        );
      }

      spotPriceToggle = (
        <Field name={`${clusterType}.useSpotPrice`}
               component={YBToggle}
               label="Use Spot Pricing"
               subLabel="spot pricing is suitable for test environments only, because spot instances might go away any time"
               onToggle={this.toggleSpotPrice}
               checkedVal={this.state.useSpotPrice}
               isReadOnly={isFieldReadOnly || this.state.gettingSuggestedSpotPrice}/>
      );
    }
    // Only enable Time Sync Service toggle for AWS.
    if (isDefinedNotNull(currentProvider) && currentProvider.code === "aws") {
      useTimeSync = (
        <Field name={`${clusterType}.useTimeSync`}
               component={YBToggle} isReadOnly={isFieldReadOnly}
               checkedVal={this.state.useTimeSync}
               onToggle={this.toggleUseTimeSync}
               label="Use AWS Time Sync"
               subLabel="Whether or not to use the Amazon Time Sync Service."/>
      );
    }

    // End spot price and EBS types

    universeProviderList.unshift(<option key="" value=""></option>);

    let universeRegionList = [];
    if (self.state.providerSelected) {
      universeRegionList =
        cloud.regions.data && cloud.regions.data.map(function (regionItem) {
          return {value: regionItem.uuid, label: regionItem.name};
        });
    }

    let universeInstanceTypeList = <option/>;

    if (currentProviderCode === "aws") {
      const optGroups = this.props.cloud.instanceTypes && this.props.cloud.instanceTypes.data.reduce(function(groups, it) {
        const prefix = it.instanceTypeCode.substr(0, it.instanceTypeCode.indexOf("."));
        groups[prefix] ? groups[prefix].push(it.instanceTypeCode): groups[prefix] = [it.instanceTypeCode];
        return groups;
      }, {});
      if (isNonEmptyObject(optGroups)) {
        universeInstanceTypeList = Object.keys(optGroups).map(function(key, idx){
          return(
            <optgroup label={`${key.toUpperCase()} type instances`} key={key+idx}>
              {
                optGroups[key].sort((a, b) => (/\d+(?!\.)/.exec(a) - /\d+(?!\.)/.exec(b)))
                  .map((item, arrIdx) => (
                    <option key={idx+arrIdx} value={item}>
                      {item}
                    </option>
                  ))
              }
            </optgroup>
          );
        });
      }
    } else if (currentProviderCode === "kubernetes")  {
      universeInstanceTypeList =
        cloud.instanceTypes.data && cloud.instanceTypes.data.map(function (instanceTypeItem, idx) {
          return (
            <option key={instanceTypeItem.instanceTypeCode}
                    value={instanceTypeItem.instanceTypeCode}>
              {instanceTypeItem.instanceTypeName || instanceTypeItem.instanceTypeCode } ({instanceTypeItem.numCores} {instanceTypeItem.numCores>1 ? "cores" : "core"}, {instanceTypeItem.memSizeGB}GB RAM)
            </option>
          );
        });
    } else {
      universeInstanceTypeList =
        cloud.instanceTypes.data && cloud.instanceTypes.data.map(function (instanceTypeItem, idx) {
          return (
            <option key={instanceTypeItem.instanceTypeCode}
                    value={instanceTypeItem.instanceTypeCode}>
              {instanceTypeItem.instanceTypeCode}
            </option>
          );
        });
    }

    let placementStatus = <span/>;
    if (self.props.universe.currentPlacementStatus) {
      placementStatus = <AZPlacementInfo placementInfo={self.props.universe.currentPlacementStatus}/>;
    }
    const azSelectorTable = (
      <div>
        <AZSelectorTable {...this.props} clusterType={clusterType}
          numNodesChangedViaAzList={this.numNodesChangedViaAzList} minNumNodes={this.state.replicationFactor}
          maxNumNodes={this.state.maxNumNodes} currentProvider={this.getCurrentProvider(currentProviderUUID)}/>
        {placementStatus}
      </div>);

    if (clusterType === "primary") {
      gflagArray =
        (<Row>
          <Col md={12}>
            <h4>G-Flags</h4>
          </Col>
          <Col md={6}>
            <FieldArray component={GFlagArrayComponent} name={`${clusterType}.masterGFlags`} flagType="master" operationType="Create" isReadOnly={isFieldReadOnly}/>
          </Col>
          <Col md={6}>
            <FieldArray component={GFlagArrayComponent} name={`${clusterType}.tserverGFlags`} flagType="tserver" operationType="Create" isReadOnly={isFieldReadOnly}/>
          </Col>
        </Row>);
    }

    const softwareVersionOptions = softwareVersions.map((item, idx) => (
      <option key={idx} value={item}>{item}</option>
    ));

    let accessKeyOptions = <option key={1} value={this.state.accessKeyCode}>{this.state.accessKeyCode}</option>;
    if (_.isObject(accessKeys) && isNonEmptyArray(accessKeys.data)) {
      accessKeyOptions = accessKeys.data.filter((key) => key.idKey.providerUUID === currentProviderUUID)
        .map((item, idx) => (
          <option key={idx} value={item.idKey.keyCode}>
            {item.idKey.keyCode}
          </option>));
    }
    let universeNameField = <span/>;
    if (clusterType === "primary") {
      universeNameField = (<Field name={`${clusterType}.universeName`}
      type="text" normalize={trimSpecialChars} component={YBTextInputWithLabel}
      label="Name" isReadOnly={isFieldReadOnly}/>);
    }

    // Instance Type is read-only if use spot price is selected
    const isInstanceTypeReadOnly = isFieldReadOnly && this.state.useSpotPrice;
    return (
      <div>
        <div className="form-section">
          <Row>
            <Col md={6}>
              <h4 style={{marginBottom: 40}}>Cloud Configuration</h4>
              <div className="form-right-aligned-labels">
                {universeNameField}
                <Field name={`${clusterType}.provider`} type="select" component={YBSelectWithLabel} label="Provider"
                        onInputChanged={this.providerChanged} options={universeProviderList} readOnlySelect={isFieldReadOnly}/>
                <Field name={`${clusterType}.regionList`} component={YBMultiSelectWithLabel} options={universeRegionList}
                      label="Regions" multi={true} selectValChanged={this.regionListChanged} providerSelected={currentProviderUUID}/>
                { clusterType === "async"
                  ? [<Field key="numNodes" name={`${clusterType}.numNodes`} type="text" component={YBControlledNumericInputWithLabel}
                      label="Nodes" onInputChanged={this.numNodesChanged} onLabelClick={this.numNodesClicked} val={this.state.numNodes}
                      minVal={Number(this.state.replicationFactor)}/>,
                    <Field key="replicationFactor" name={`${clusterType}.replicationFactor`} type="text" component={YBRadioButtonBarWithLabel} options={[1, 2, 3, 4, 5, 6, 7]}
                      label="Replication Factor" initialValue={this.state.replicationFactor} onSelect={this.replicationFactorChanged} isReadOnly={isFieldReadOnly}/>]
                  : null
                }
              </div>

              { clusterType !== "async" &&
                <Row>
                  <div className="form-right-aligned-labels">
                    <Col lg={5}>
                      <Field name={`${clusterType}.numNodes`} type="text" component={YBControlledNumericInputWithLabel}
                            label="Nodes" onInputChanged={this.numNodesChanged} onLabelClick={this.numNodesClicked} val={this.state.numNodes}
                            minVal={Number(this.state.replicationFactor)}/>
                    </Col>
                    <Col lg={7} className="button-group-row">
                      <Field name={`${clusterType}.replicationFactor`} type="text" component={YBRadioButtonBarWithLabel} options={[1, 3, 5, 7]}
                            label="Replication Factor" initialValue={this.state.replicationFactor} onSelect={this.replicationFactorChanged} isReadOnly={isFieldReadOnly}/>

                    </Col>
                  </div>
                </Row>
              }
            </Col>
            <Col md={6} className={"universe-az-selector-container"}>
              {azSelectorTable}
            </Col>
          </Row>
        </div>
        <div className="form-section">
          <Row>
            <Col md={12}>
              <h4>Instance Configuration</h4>
            </Col>
          </Row>
          <Row>
            <Col sm={12} md={12} lg={6}>
              <div className="form-right-aligned-labels">
                <Field name={`${clusterType}.instanceType`} component={YBSelectWithLabel} label="Instance Type"
                       options={universeInstanceTypeList} onInputChanged={this.instanceTypeChanged} readOnlySelect={isInstanceTypeReadOnly}/>
              </div>
            </Col>
            <Col sm={12} md={12} lg={6}>
              {deviceDetail &&
                <div className="form-right-aligned-labels">
                  <div className="form-inline-controls">
                    <div className="form-group universe-form-instance-info">
                      <label className="form-item-label form-item-label-shrink">Volume Info</label>
                      {deviceDetail}
                    </div>
                  </div>
                  <div className="form-inline-controls">
                    <div className="form-group universe-form-instance-info">
                      {iopsField}
                    </div>
                  </div>
                  <div className="form-inline-controls">
                    <div className="form-group universe-form-instance-info">
                      {ebsTypeSelector}
                    </div>
                  </div>
                </div>
              }
            </Col>
            <Col sm={12} md={12} lg={6}>
              <div className="form-right-aligned-labels">
                {storageClassField}
                {spotPriceToggle}
                {spotPriceField}
                {assignPublicIP}
                {useTimeSync}
                <Field name={`${clusterType}.mountPoints`} component={YBTextInput}  type="hidden"/>
              </div>
            </Col>
          </Row>
        </div>
        <div className="form-section">
          <Row>
            <Col md={12}>
              <h4>Advanced</h4>
            </Col>
            <Col sm={5} md={4}>
              <div className="form-right-aligned-labels">
                <Field name={`${clusterType}.ybSoftwareVersion`} component={YBSelectWithLabel}
                       options={softwareVersionOptions} label="DB Version" onInputChanged={this.softwareVersionChanged} readOnlySelect={isFieldReadOnly}/>
              </div>
            </Col>
            <Col lg={4}>
              <div className="form-right-aligned-labels">
                <Field name={`${clusterType}.accessKeyCode`} type="select" component={YBSelectWithLabel} label="Access Key"
                       onInputChanged={this.accessKeyChanged} options={accessKeyOptions} readOnlySelect={isFieldReadOnly}/>
              </div>
            </Col>
          </Row>
        </div>
        <div className="form-section no-border">
          {gflagArray}
        </div>
      </div>
    );
  }
}

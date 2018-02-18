// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { Row, Col, Grid } from 'react-bootstrap';
import { Field, change, FieldArray } from 'redux-form';
import {browserHistory, withRouter} from 'react-router';
import _ from 'lodash';
import { isDefinedNotNull, isNonEmptyObject, isNonEmptyString, areIntentsEqual, isEmptyObject, isNonEmptyArray,
          normalizeToPositiveFloat } from 'utils/ObjectUtils';
import { YBTextInputWithLabel, YBControlledNumericInput, YBControlledNumericInputWithLabel,
          YBSelectWithLabel, YBControlledSelectWithLabel, YBMultiSelectWithLabel, YBRadioButtonBarWithLabel,
          YBButton, YBToggle } from 'components/common/forms/fields';
import {getPromiseState} from 'utils/PromiseUtils';
import AZSelectorTable from './AZSelectorTable';
import { UniverseResourcesNew as UniverseResources } from '../UniverseResources';
import './UniverseForm.scss';
import AZPlacementInfo from './AZPlacementInfo';
import GFlagArrayComponent from './GFlagArrayComponent';
import { IN_DEVELOPMENT_MODE } from '../../../config';
import {getPrimaryCluster} from "../../../utils/UniverseUtils";

const initialState = {
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
  maxNumNodes: -1, // Maximum Number of nodes currently in use OnPrem case
  useSpotPrice: IN_DEVELOPMENT_MODE,
  spotPrice: normalizeToPositiveFloat('0.00'),
  gettingSuggestedSpotPrice: false
};

const DEFAULT_INSTANCE_TYPE_MAP = {
  'aws': 'c4.2xlarge',
  'gcp': 'n1-standard-1'
};

class UniverseForm extends Component {
  static propTypes = {
    type: PropTypes.oneOf(['Edit', 'Create']).isRequired
  };

  constructor(props, context) {
    super(props);
    this.state = initialState;
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
  };

  handleSubmitButtonClick = () => {
    const {type} = this.props;
    if (type === "Create") {
      this.createUniverse();
    } else {
      this.editUniverse();
    }
  };

  getCurrentProvider = providerUUID => {
    return this.props.cloud.providers.data.find((provider) => provider.uuid === providerUUID);
  };

  getCurrentUserIntent = () => {
    const {formValues} = this.props;
    return {
      universeName: formValues.universeName,
      numNodes: this.state.numNodes,
      provider: this.state.providerSelected,
      regionList: this.state.regionList,
      instanceType: this.state.instanceTypeSelected,
      ybSoftwareVersion: this.state.ybSoftwareVersion,
      replicationFactor: this.state.replicationFactor,
      deviceInfo: this.state.deviceInfo,
      accessKeyCode: this.state.accessKeyCode,
      gflags: this.state.gflags,
      spotPrice: this.state.spotPrice
    };
  };

  setDeviceInfo = (instanceTypeCode, instanceTypeList) => {
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
      this.setState({nodeSetViaAZList: false, deviceInfo: deviceInfo, volumeType: volumeDetail.volumeType});
    }
  };

  configureUniverseNodeList = () => {
    const {universe: {universeConfigTemplate, currentUniverse}, formValues} = this.props;

    let universeTaskParams = {};
    if (isNonEmptyObject(universeConfigTemplate.data)) {
      universeTaskParams = _.clone(universeConfigTemplate.data, true);
    }

    if (isNonEmptyObject(currentUniverse.data)) {
      universeTaskParams.universeUUID = currentUniverse.data.universeUUID;
      universeTaskParams.expectedUniverseVersion = currentUniverse.data.version;
    }
    const currentState = this.state;
    const userIntent = {
      universeName: formValues.universeName,
      provider: currentState.providerSelected,
      regionList: currentState.regionList,
      numNodes: currentState.numNodes,
      instanceType: currentState.instanceTypeSelected,
      ybSoftwareVersion: currentState.ybSoftwareVersion,
      replicationFactor: currentState.replicationFactor,
      deviceInfo: currentState.deviceInfo,
      accessKeyCode: currentState.accessKeyCode,
      spotPrice: currentState.spotPrice
    };

    if (isNonEmptyObject(formValues.masterGFlags)) {
      userIntent["masterGFlags"] = formValues.masterGFlags;
    }
    if (isNonEmptyObject(formValues.tserverGFlags)) {
      userIntent["tserverGFlags"] = formValues.tserverGFlags;
    }
    if (isDefinedNotNull(currentState.instanceTypeSelected) && isNonEmptyArray(currentState.regionList)) {
      this.props.cloud.providers.data.forEach(function (providerItem) {
        if (providerItem.uuid === userIntent.provider) {
          userIntent.providerType = providerItem.code;
        }
      });
      userIntent.regionList = formValues.regionList.map(item => item.value);
      const primaryCluster = getPrimaryCluster(universeTaskParams.clusters);
      if (isDefinedNotNull(primaryCluster)) {
        primaryCluster.userIntent = userIntent;
      } else {
        universeTaskParams.clusters = [{clusterType: 'PRIMARY', userIntent: userIntent}];
      }
      this.handleUniverseConfigure(universeTaskParams);
    }
  };

  handleUniverseConfigure = universeTaskParams => {
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
  };

  createUniverse = () => {
    this.props.submitCreateUniverse(this.getFormPayload());
  };

  editUniverse = () => {
    const {universe: {currentUniverse: {data: {universeUUID}}}} = this.props;
    this.props.submitEditUniverse(this.getFormPayload(), universeUUID);
  };

  componentWillMount() {
    this.props.resetConfig();
    if (isNonEmptyArray(this.props.softwareVersions)) {
      this.setState({ybSoftwareVersion: this.props.softwareVersions[0]});
    }
    if (this.props.type === "Edit") {
      const {universe: {currentUniverse: {data: {universeDetails}}}} = this.props;
      const primaryCluster = getPrimaryCluster(universeDetails.clusters);
      const userIntent = primaryCluster && primaryCluster.userIntent;
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
    }
  }

  providerChanged = value => {
    const providerUUID = value;
    if (isEmptyObject(this.props.universe.currentUniverse.data)) {
      this.props.resetConfig();
      this.props.dispatch(change("UniverseForm", "regionList", []));
      //If we have accesskeys for a current selected provider we set that in the state or we fallback to default value.
      let defaultAccessKeyCode = initialState.accessKeyCode;
      if (isNonEmptyArray(this.props.accessKeys.data)) {
        const providerAccessKeys = this.props.accessKeys.data.filter((key) => key.idKey.providerUUID === value);
        if (isNonEmptyArray(providerAccessKeys)) {
          defaultAccessKeyCode = providerAccessKeys[0].idKey.keyCode;
        }
      }
      this.setState({nodeSetViaAZList: false, regionList: [], providerSelected: providerUUID,
        deviceInfo: {}, accessKeyCode: defaultAccessKeyCode});
      this.props.getRegionListItems(providerUUID, this.state.azCheckState);
      this.props.getInstanceTypeListItems(providerUUID);
    }
    const currentProviderData = this.getCurrentProvider(value);
    if (currentProviderData && currentProviderData.code === "onprem") {
      this.props.fetchNodeInstanceList(value);
    }
  };

  getSuggestedSpotPrice = (instanceType, regions) => {
    const currentProvider = this.getCurrentProvider(this.state.providerSelected);
    const regionUUIDs = regions.map(region => region.value);
    if (this.props.type !== "Edit" && isDefinedNotNull(currentProvider) && currentProvider.code === "aws"
        && isNonEmptyArray(regionUUIDs)) {
      this.props.getSuggestedSpotPrice(this.state.providerSelected, instanceType, regionUUIDs);
      this.setState({gettingSuggestedSpotPrice: true});
    }
  };

  regionListChanged = value => {
    this.setState({nodeSetViaAZList: false, regionList: value});
    if (this.state.useSpotPrice) {
      this.getSuggestedSpotPrice(this.state.instanceTypeSelected, value);
    } else {
      this.props.resetSuggestedSpotPrice();
    }
  };

  instanceTypeChanged = event => {
    const instanceTypeValue = event.target.value;
    this.setState({instanceTypeSelected: instanceTypeValue});
    this.setDeviceInfo(instanceTypeValue, this.props.cloud.instanceTypes.data);
    if (this.state.useSpotPrice) {
      this.getSuggestedSpotPrice(instanceTypeValue, this.state.regionList);
    } else {
      this.props.resetSuggestedSpotPrice();
    }
  };

  numNodesChanged = value => {
    this.setState({numNodes: value});
  };

  numNodesChangedViaAzList = value => {
    this.setState({nodeSetViaAZList: true, numNodes: value});
  };

  numNodesClicked = () => {
    this.setState({nodeSetViaAZList: false});
  };

  azChanged = event => {
    this.setState({azCheckState: !this.state.azCheckState});
  };

  softwareVersionChanged = version => {
    this.setState({ybSoftwareVersion: version, nodeSetViaAZList: false});
  };

  replicationFactorChanged = value => {
    const self = this;
    if (isEmptyObject(this.props.universe.currentUniverse.data)) {
      this.setState({nodeSetViaAZList: false, replicationFactor: value}, function () {
        if (self.state.numNodes <= value) {
          self.setState({numNodes: value});
        }
      });
    }
  };

  componentWillUpdate(newProps) {
    if (newProps.universe.formSubmitSuccess) {
      this.props.reset();
    }
  }

  // Compare state variables against existing user intent
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

  componentDidUpdate(prevProps, prevState) {
    const {universe: {currentUniverse}} = this.props;
    const currentProvider = this.getCurrentProvider(this.state.providerSelected);
    // Fire Configure only iff either provider is not on-prem or maxNumNodes is not -1 if on-prem
    if (!_.isEqual(this.state, prevState) && isNonEmptyObject(currentProvider) && (prevState.maxNumNodes !== -1 || currentProvider.code !== "onprem")) {
      if (((currentProvider.code === "onprem" && this.state.numNodes <= this.state.maxNumNodes) || (currentProvider.code !== "onprem"))
          && (this.state.numNodes >= this.state.replicationFactor && !this.state.nodeSetViaAZList)) {
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
      } else if (isNonEmptyArray(this.state.regionList) &&
        currentProvider.code === "onprem" && this.state.instanceTypeSelected &&
        this.state.numNodes >= this.state.maxNumNodes) {
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
  }

  ebsTypeChanged = event => {
    const currentDeviceInfo = _.clone(this.state.deviceInfo);
    currentDeviceInfo.ebsType = event.target.value;
    if (currentDeviceInfo.ebsType === "IO1" && currentDeviceInfo.diskIops == null) {
      currentDeviceInfo.diskIops = 1000;
    } else {
      currentDeviceInfo.diskIops = null;
    }
    this.setState({deviceInfo: currentDeviceInfo, ebsType: event.target.value});
  };

  numVolumesChanged = val => {
    this.setState({deviceInfo: {...this.state.deviceInfo, numVolumes: val}});
  };

  volumeSizeChanged = val => {
    this.setState({deviceInfo: {...this.state.deviceInfo, volumeSize: val}});
  };

  diskIopsChanged = val => {
    if (this.state.deviceInfo.ebsType === "IO1") {
      this.setState({deviceInfo: {...this.state.deviceInfo, diskIops: val}});
    }
  };

  getFormPayload = () => {
    const {formValues, universe: {universeConfigTemplate}} = this.props;
    const submitPayload = _.clone(universeConfigTemplate.data, true);
    submitPayload.clusters.forEach((cluster) => {
      cluster.userIntent.universeName = formValues.universeName;
      cluster.userIntent.spotPrice = 0.0;
      if (this.state.useSpotPrice) {
        cluster.userIntent.spotPrice = parseFloat(this.state.spotPrice);
      }
      cluster.userIntent.assignPublicIP = formValues.assignPublicIP;

      cluster.userIntent.masterGFlags = formValues.masterGFlags.filter((masterFlag) => {
        return isNonEmptyString(masterFlag.name) && isNonEmptyString(masterFlag.value);
      }).map((masterFlag) => {
        return {name: masterFlag.name, value: masterFlag.value};
      });

      cluster.userIntent.tserverGFlags = formValues.tserverGFlags.filter((tserverFlag) => {
        return isNonEmptyString(tserverFlag.name) && isNonEmptyString(tserverFlag.value);
      }).map((tserverFlag) => {
        return {name: tserverFlag.name, value: tserverFlag.value};
      });
    });

    return submitPayload;
  };

  toggleSpotPrice = event => {
    const nextState = {useSpotPrice: event.target.checked};
    if (event.target.checked) {
      this.getSuggestedSpotPrice(this.state.instanceTypeSelected, this.state.regionList);
    } else {
      nextState['spotPrice'] = initialState.spotPrice;
      this.props.resetSuggestedSpotPrice();
    }
    this.setState(nextState);
  };

  spotPriceChanged = (val, normalize) => {
    this.setState({spotPrice: normalize ? normalizeToPositiveFloat(val) : val});
  };

  componentWillReceiveProps(nextProps) {
    const {
      universe: {showModal, visibleModal, currentUniverse},
      cloud: {nodeInstanceList, instanceTypes, suggestedSpotPrice}
    } = nextProps;

    if (nextProps.cloud.instanceTypes.data !== this.props.cloud.instanceTypes.data
      && isNonEmptyArray(nextProps.cloud.instanceTypes.data) && this.state.providerSelected
      && nextProps.type !== "Edit") {
      let instanceTypeSelected = null;
      const currentProviderCode = this.getCurrentProvider(this.state.providerSelected).code;
      instanceTypeSelected = DEFAULT_INSTANCE_TYPE_MAP[currentProviderCode];
      // If we have the default instance type in the cloud instance types then we
      // use it, otherwise we pick the first one in the list and use it.
      const hasInstanceType = instanceTypes.data.find( (it) => {
        return it.providerCode === currentProviderCode && it.instanceTypeCode === instanceTypeSelected;
      });
      if (!hasInstanceType) {
        instanceTypeSelected = instanceTypes.data[0].instanceTypeCode;
      }
      this.setState({instanceTypeSelected: instanceTypeSelected});
      this.setDeviceInfo(instanceTypeSelected, instanceTypes.data);
    }

    // Set default ebsType once API call has completed
    if (isNonEmptyArray(nextProps.cloud.ebsTypes) && !isNonEmptyArray(this.props.cloud.ebsTypes)) {
      this.setState({"ebsType": "GP2"});
    }

    // Set Default Software Package in case of Create
    if (nextProps.softwareVersions !== this.props.softwareVersions
      && isEmptyObject(this.props.universe.currentUniverse.data)
      && isNonEmptyArray(nextProps.softwareVersions)
      && !isNonEmptyArray(this.props.softwareVersions)) {
      this.setState({ybSoftwareVersion: nextProps.softwareVersions[0]});
    }

    // Set spot price
    const currentPromiseState = getPromiseState(this.props.cloud.suggestedSpotPrice);
    const nextPromiseState = getPromiseState(suggestedSpotPrice);
    if (currentPromiseState.isInit() || currentPromiseState.isLoading()) {
      if (nextPromiseState.isSuccess()) {
        this.setState({
          spotPrice: normalizeToPositiveFloat(suggestedSpotPrice.data.toString()),
          useSpotPrice: true,
          gettingSuggestedSpotPrice: false
        });
      } else if (nextPromiseState.isError()) {
        this.setState({
          spotPrice: normalizeToPositiveFloat('0.00'),
          useSpotPrice: false,
          gettingSuggestedSpotPrice: false
        });
      }
    }

    // If dialog has been closed and opened again in-case of edit, then repopulate current config
    if (isNonEmptyObject(currentUniverse.data) && showModal && !this.props.universe.showModal &&
        visibleModal === "universeModal") {
      const primaryCluster = getPrimaryCluster(currentUniverse.data.universeDetails.clusters);
      if (isDefinedNotNull(primaryCluster)) {
        const userIntent = primaryCluster.userIntent;
        this.props.getExistingUniverseConfiguration(currentUniverse.data.universeDetails);
        const isMultiAZ = true;
        if (isNonEmptyObject(userIntent) && isNonEmptyObject(userIntent.provider)) {
          this.setState({
            providerSelected: userIntent.provider,
            azCheckState: isMultiAZ,
            instanceTypeSelected: userIntent.instanceType,
            numNodes: userIntent.numNodes,
            replicationFactor: userIntent.replicationFactor,
            ybSoftwareVersion: userIntent.ybSoftwareVersion,
            regionList: userIntent.regionList,
            accessKeyCode: userIntent.accessKeyCode,
            deviceInfo: userIntent.deviceInfo
          });
        }
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
    if (getPromiseState(this.props.universe.editUniverse).isLoading() && getPromiseState(nextProps.universe.editUniverse).isSuccess()) {
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
      if (this.props.type === "Edit") {
        const primaryCluster = getPrimaryCluster(currentUniverse.data.universeDetails.clusters);
        if (isDefinedNotNull(primaryCluster)) {
          numNodesAvailable += primaryCluster.userIntent.numNodes;
        }
      }
      this.setState({maxNumNodes: numNodesAvailable});
    }
  }

  render() {
    const self = this;
    const {handleSubmit, universe, softwareVersions, accessKeys, type, cloud, cloud: {suggestedSpotPrice}} = this.props;
    let universeProviderList = [];
    let currentProviderCode = "";
    if (isNonEmptyArray(cloud.providers.data)) {
      universeProviderList = cloud.providers.data.map(function(providerItem, idx) {
        if (providerItem.uuid === self.state.providerSelected) {
          currentProviderCode = providerItem.code;
        }
        return (
          <option key={providerItem.uuid} value={providerItem.uuid}>
            {providerItem.name}
          </option>
        );
      });
    }
    universeProviderList.unshift(<option key="" value=""></option>);

    const ebsTypesList = cloud.ebsTypes && cloud.ebsTypes.map(function (ebsType, idx) {
      return <option key={ebsType} value={ebsType}>{ebsType}</option>;
    });

    const universeRegionList = cloud.regions.data && cloud.regions.data.map(function (regionItem, idx) {
      return {value: regionItem.uuid, label: regionItem.name};
    });

    let universeInstanceTypeList = <option/>;
    if (currentProviderCode === "aws") {
      const optGroups = this.props.cloud.instanceTypes.data.reduce(function(groups, it) {
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
    if (isNonEmptyArray(universeInstanceTypeList)) {
      universeInstanceTypeList.unshift(<option key="" value="">Select</option>);
    }

    let submitLabel;
    if (this.props.type === "Create") {
      submitLabel = 'Create';
    } else {
      submitLabel = 'Save';
    }

    const softwareVersionOptions = softwareVersions.map((item, idx) => (
      <option key={idx} value={item}>{item}</option>
    ));

    let accessKeyOptions = <option key={1} value={this.state.accessKeyCode}>{this.state.accessKeyCode}</option>;
    if (_.isObject(accessKeys) && isNonEmptyArray(accessKeys.data)) {
      accessKeyOptions = accessKeys.data.filter((key) => key.idKey.providerUUID === self.state.providerSelected)
                                        .map((item, idx) => (
                                          <option key={idx} value={item.idKey.keyCode}>
                                            {item.idKey.keyCode}
                                          </option>));
    }

    let placementStatus = <span/>;
    if (self.props.universe.currentPlacementStatus) {
      placementStatus = <AZPlacementInfo placementInfo={self.props.universe.currentPlacementStatus}/>;
    }

    let ebsTypeSelector = <span/>;
    let deviceDetail = null;
    let iopsField = <span/>;
    function volumeTypeFormat(num) {
      return num + ' GB';
    }
    const isFieldReadOnly = isNonEmptyObject(universe.currentUniverse.data) && this.props.type === "Edit";
    if (_.isObject(self.state.deviceInfo) && isNonEmptyObject(self.state.deviceInfo)) {
      if (self.state.volumeType === 'EBS') {
        if (self.state.deviceInfo.ebsType === 'IO1') {
          iopsField = (
            <span className="volume-info form-group-shrinked">
              <label className="form-item-label">Provisioned IOPS</label>
              <span className="volume-info-field volume-info-iops">
                <Field name="diskIops" component={YBControlledNumericInput} label="Provisioned IOPS"
                       val={self.state.deviceInfo.diskIops} onInputChanged={self.diskIopsChanged}/>
              </span>
            </span>
          );
        }
        deviceDetail = (
          <span className="volume-info">
            <span className="volume-info-field volume-info-count">
              <Field name="volumeCount" component={YBControlledNumericInput}
                     label="Number of Volumes" val={self.state.deviceInfo.numVolumes} onInputChanged={self.numVolumesChanged}/>
            </span>
            &times;
            <span className="volume-info-field volume-info-size">
              <Field name="volumeSize" component={YBControlledNumericInput} label="Volume Size" val={self.state.deviceInfo.volumeSize}
                     valueFormat={volumeTypeFormat} onInputChanged={self.volumeSizeChanged}/>
            </span>
          </span>
        );
        ebsTypeSelector = (
          <span className="volume-info form-group-shrinked">
            <Field name="ebsType" component={YBControlledSelectWithLabel} options={ebsTypesList}
                   label="EBS Type" selectVal={self.state.ebsType}
                   onInputChanged={self.ebsTypeChanged}/>
          </span>
        );
      } else if (self.state.volumeType === 'SSD') {
        let mountPointsDetail = <span />;
        if (self.state.deviceInfo.mountPoints != null) {
          mountPointsDetail = (
            <span>
              <label className="form-item-label">Mount Points</label>
              {self.state.deviceInfo.mountPoints}
            </span>
          );
        }
        deviceDetail = (
          <span className="volume-info">
            {self.state.deviceInfo.numVolumes} &times;&nbsp;
            {volumeTypeFormat(self.state.deviceInfo.volumeSize)} {self.state.volumeType} &nbsp;
            {mountPointsDetail}
          </span>
        );
      }
    }

    let spotPriceToggle = <span />;
    let spotPriceField = <span />;
    let assignPublicIP = <span />;
    const currentProvider = this.getCurrentProvider(this.state.providerSelected);
    if (isDefinedNotNull(currentProvider) && currentProvider.code === "aws"
        && isDefinedNotNull(self.props.universe.currentPlacementStatus)) {

      assignPublicIP = (
        <Field name="assignPublicIP"
               component={YBToggle}
               label="Assign Public IP"
               subLabel="Whether or not to assign a public IP."
               checkedVal={this.state.assignPublicIP}/>
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
          <Field name="spotPrice" type="text"
                 component={YBTextInputWithLabel}
                 label="Spot Price (Per Hour)"
                 isReadOnly={isFieldReadOnly || !this.state.useSpotPrice}
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
        <Field name="useSpotPrice"
               component={YBToggle}
               label="Use Spot Pricing"
               subLabel="spot pricing is suitable for test environments only, because spot instances might go away any time"
               onToggle={this.toggleSpotPrice}
               checkedVal={this.state.useSpotPrice}
               isReadOnly={isFieldReadOnly || this.state.gettingSuggestedSpotPrice}/>
      );
    }
    const pageTitle = this.props.type === "Create" ? <h2 className="content-title">{this.props.type.toLowerCase()} universe</h2> : <h2 className="content-title">{this.props.formValues.universeName}<span> - {this.props.type.toLowerCase()} universe </span></h2>;
    return (

      <Grid id="page-wrapper" fluid={true} className="universe-form-new">
        {pageTitle}
        <form name="UniverseForm" className="universe-form-container" onSubmit={handleSubmit(this.handleSubmitButtonClick)}>
          <div className="form-section">
            <Row>
              <Col md={6}>
                <h4 style={{marginBottom: 40}}>Cloud Configuration</h4>
                <div className="form-right-aligned-labels">
                  <Field name="universeName" type="text" component={YBTextInputWithLabel} label="Name"
                        isReadOnly={isFieldReadOnly} />
                  <Field name="provider" type="select" component={YBSelectWithLabel} label="Provider"
                        options={universeProviderList} onInputChanged={this.providerChanged} readOnlySelect={isFieldReadOnly} />
                  <Field name="regionList" component={YBMultiSelectWithLabel}
                        label="Regions" options={universeRegionList}
                        selectValChanged={this.regionListChanged} multi={this.state.azCheckState}
                        providerSelected={this.state.providerSelected}/>
                  <Field name="numNodes" type="text" component={YBControlledNumericInputWithLabel}
                        label="Nodes" onInputChanged={this.numNodesChanged} onLabelClick={this.numNodesClicked} val={this.state.numNodes}
                        minVal={Number(this.state.replicationFactor)}/>
                </div>
              </Col>
              <Col md={6} className={"universe-az-selector-container"}>
                <AZSelectorTable {...this.props} setPlacementInfo={this.setPlacementInfo}
                                numNodesChangedViaAzList={this.numNodesChangedViaAzList} minNumNodes={this.state.replicationFactor}
                                maxNumNodes={this.state.maxNumNodes} currentProvider={this.getCurrentProvider(this.state.providerSelected)}/>
                {placementStatus}
              </Col>
            </Row>
          </div>
          <div className="form-section">
            <Row>
              <Col md={12}>
                <h4>Instance Configuration</h4>
              </Col>
              <Col sm={12} md={12} lg={6}>
                <div className="form-right-aligned-labels">
                  <Field name="instanceType" type="select" component={YBControlledSelectWithLabel} label="Instance Type"
                        options={universeInstanceTypeList} selectVal={this.state.instanceTypeSelected}
                        onInputChanged={this.instanceTypeChanged} isReadOnly={isFieldReadOnly && this.state.useSpotPrice}/>
                  {spotPriceToggle}
                  {spotPriceField}
                  {assignPublicIP}
                </div>
              </Col>
              {deviceDetail &&
              <Col sm={12} md={12} lg={6}>
                <div className="form-right-aligned-labels form-inline-controls">
                  <div className="form-group universe-form-instance-info">
                    <label className="form-item-label form-item-label-shrink">Volume Info</label>
                    {deviceDetail}
                  </div>
                </div>
                { self.state.deviceInfo.ebsType === 'IO1' &&
                  <div className="form-right-aligned-labels form-inline-controls">
                    <div className="form-group universe-form-instance-info">
                      {iopsField}
                    </div>
                  </div>
                }
                <div className="form-right-aligned-labels form-inline-controls">
                  <div className="form-group universe-form-instance-info">
                    {ebsTypeSelector}
                  </div>
                </div>
              </Col>
              }
            </Row>
          </div>
          <div className="form-section">
            <Row>
              <Col md={12}>
                <h4>Advanced</h4>
              </Col>
              <Col sm={7} md={4}>
                <div className="form-right-aligned-labels replication-factor-field">
                  <Field name="replicationFactor" type="text" component={YBRadioButtonBarWithLabel} options={[1, 3, 5, 7]}
                        label="Replication Factor" initialValue={this.state.replicationFactor}
                        onSelect={this.replicationFactorChanged} isReadOnly={isFieldReadOnly}/>
                </div>
              </Col>
              <Col sm={5} md={4}>
                <div className="form-right-aligned-labels">
                  <Field name="ybSoftwareVersion" type="select" component={YBSelectWithLabel} defaultValue={this.state.ybSoftwareVersion}
                        options={softwareVersionOptions} label="YugaByte Version" onInputChanged={this.softwareVersionChanged} readOnlySelect={isFieldReadOnly}/>
                </div>
              </Col>
              <Col lg={4}>
                <div className="form-right-aligned-labels">
                  <Field name="accessKeyCode" type="select" component={YBSelectWithLabel} label="Access Key"
                        defaultValue={this.state.accessKeyCode} options={accessKeyOptions} readOnlySelect={isFieldReadOnly}/>
                </div>
              </Col>
            </Row>
          </div>
          <div className="form-section no-border">
            <Row>
              <Col md={12}>
                <h4>G-Flags</h4>
              </Col>
              <Col md={6}>
                <FieldArray component={GFlagArrayComponent} name="masterGFlags" flagType="master" operationType={type}/>
              </Col>
              <Col md={6}>
                <FieldArray component={GFlagArrayComponent} name="tserverGFlags" flagType="tserver" operationType={type}/>
              </Col>
            </Row>
          </div>
          <div className="form-action-button-container">
            <UniverseResources resources={universe.universeResourceTemplate.data}>
              <YBButton btnClass="btn btn-default universe-form-submit-btn" btnText="Cancel" onClick={this.handleCancelButtonClick}/>
              <YBButton btnClass="btn btn-orange universe-form-submit-btn" btnText={submitLabel} btnType={"submit"}/>
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

export default withRouter(UniverseForm);

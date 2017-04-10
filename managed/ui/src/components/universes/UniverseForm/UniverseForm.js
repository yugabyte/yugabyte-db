import React, { Component, PropTypes } from 'react';
import { Row, Col } from 'react-bootstrap';
import { Field, change } from 'redux-form';
import _ from 'lodash';
import { isDefinedNotNull, isValidArray, isValidObject, areIntentsEqual } from 'utils/ObjectUtils';
import { YBModal, YBTextInputWithLabel, YBControlledNumericInput, YBControlledNumericInputWithLabel,
  YBSelectWithLabel, YBMultiSelectWithLabel, YBRadioButtonBarWithLabel } from 'components/common/forms/fields';

import AZSelectorTable from './AZSelectorTable';
import { UniverseResources } from '../UniverseResources';
import './UniverseForm.scss';
import AZPlacementInfo from './AZPlacementInfo';

const initialState = {
  instanceTypeSelected: 'm3.medium',
  azCheckState: true,
  providerSelected: '',
  regionList: [],
  numNodes: 3,
  isCustom: false,
  replicationFactor: 3,
  deviceInfo: {},
  placementInfo: {},
  ybSoftwareVersion: '',
  accessKeyCode: 'yugabyte-default'
};

export default class UniverseForm extends Component {
  static propTypes = {
    type: PropTypes.oneOf(['Edit', 'Create']).isRequired,
    title: PropTypes.string.isRequired,
  }

  constructor(props) {
    super(props);
    this.providerChanged = this.providerChanged.bind(this);
    this.regionListChanged = this.regionListChanged.bind(this);
    this.instanceTypeChanged = this.instanceTypeChanged.bind(this);
    this.numNodesChanged = this.numNodesChanged.bind(this);
    this.createUniverse = this.createUniverse.bind(this);
    this.editUniverse = this.editUniverse.bind(this);
    this.softwareVersionChanged = this.softwareVersionChanged.bind(this);
    this.azChanged = this.azChanged.bind(this);
    this.numNodesChangedViaAzList = this.numNodesChangedViaAzList.bind(this);
    this.numNodesClicked = this.numNodesClicked.bind(this);
    this.replicationFactorChanged = this.replicationFactorChanged.bind(this);
    this.volumeSizeChanged = this.volumeSizeChanged.bind(this);
    this.numVolumesChanged = this.numVolumesChanged.bind(this);
    this.diskIopsChanged = this.diskIopsChanged.bind(this);
    this.hideModal = this.hideModal.bind(this);
    this.configureUniverseNodeList = this.configureUniverseNodeList.bind(this);
    this.state = initialState;
  }

  hideModal() {
    this.setState(initialState);
    this.props.reset();
    this.props.onHide();
  }

  configureUniverseNodeList() {
    const {universe: {universeConfigTemplate, currentUniverse}, formValues} = this.props;
    var universeTaskParams = _.clone(universeConfigTemplate, true);
    if (isDefinedNotNull(currentUniverse)) {
      universeTaskParams.universeUUID = currentUniverse.universeUUID;
      universeTaskParams.expectedUniverseVersion = currentUniverse.version;
    }
    var currentState = this.state;
    universeTaskParams.userIntent = {
      universeName: formValues.universeName,
      provider: currentState.providerSelected,
      regionList: currentState.regionList,
      numNodes: currentState.numNodes,
      instanceType: currentState.instanceTypeSelected,
      ybSoftwareVersion: currentState.ybSoftwareVersion,
      replicationFactor: currentState.replicationFactor,
      isMultiAZ: true,
      deviceInfo: currentState.deviceInfo,
      accessKeyCode: currentState.accessKeyCode
    }
    if (isDefinedNotNull(currentState.instanceTypeSelected) && isValidArray(currentState.regionList)) {
      this.props.cloud.providers.forEach(function (providerItem, idx) {
        if (providerItem.uuid === universeTaskParams.userIntent.provider) {
          universeTaskParams.userIntent.providerType = providerItem.code;
        }
      });
      if (!isValidArray(universeTaskParams.userIntent.regionList)) {
        universeTaskParams.userIntent.regionList = [formValues.regionList.value];
      } else {
        universeTaskParams.userIntent.regionList = formValues.regionList.map(function (item, idx) {
          return item.value;
        });
      }
      if (isValidObject(universeTaskParams.placementInfo)) {
        universeTaskParams.placementInfo.isCustom = this.state.isCustom;
      }
      if (currentUniverse) {
        if (!areIntentsEqual(currentUniverse.universeDetails.userIntent, universeTaskParams.userIntent)) {
          this.props.submitConfigureUniverse(universeTaskParams);
        } else {
          this.props.getExistingUniverseConfiguration(currentUniverse.universeDetails);
        }
      } else {
        this.props.submitConfigureUniverse(universeTaskParams);
      }
    }
  }

  createUniverse() {
    this.props.submitCreateUniverse(this.props.universe.universeConfigTemplate);
  }

  editUniverse() {
    const {universe: {universeConfigTemplate, currentUniverse: {universeUUID}}} = this.props;
    this.props.submitEditUniverse(universeConfigTemplate, universeUUID);
  }

  componentWillMount() {
    this.props.resetConfig();
    if (isValidArray(this.props.softwareVersions)) {
      this.setState({ybSoftwareVersion: this.props.softwareVersions[0]});
    }
    if (this.props.type === "Edit") {
      const {universe: {currentUniverse}, universe: {currentUniverse: {universeDetails: {userIntent}}}} = this.props;
      var providerUUID = currentUniverse.provider && currentUniverse.provider.uuid;
      var isMultiAZ = userIntent.isMultiAZ;
      if (userIntent && providerUUID) {
        this.setState({
          providerSelected: providerUUID,
          azCheckState: isMultiAZ,
          instanceTypeSelected: userIntent.instanceType,
          numNodes: userIntent.numNodes,
          replicationFactor: userIntent.replicationFactor,
          ybSoftwareVersion: userIntent.ybSoftwareVersion,
          accessKeyCode: userIntent.accessKeyCode
        });
      }
      this.props.getRegionListItems(providerUUID, isMultiAZ);
      this.props.getInstanceTypeListItems(providerUUID);
      // If Edit Case Set Initial Configuration
      this.props.getExistingUniverseConfiguration(currentUniverse.universeDetails);
    }
  }

  providerChanged(value) {
    var providerUUID = value;
    if (!this.props.universe.currentUniverse) {
      this.props.resetConfig();
      this.props.dispatch(change("UniverseForm", "regionList", []));
      this.setState({regionList: [], providerSelected: providerUUID, deviceInfo: {}});
      this.props.getRegionListItems(providerUUID, this.state.azCheckState);
      this.props.getInstanceTypeListItems(providerUUID);
    }
  }

  regionListChanged(value) {
    this.setState({regionList: value});
  }

  instanceTypeChanged(instanceTypeValue) {
    this.setState({instanceTypeSelected: instanceTypeValue});
    var instanceTypeSelected = this.props.cloud.instanceTypes.find(function(item){
      return item.instanceTypeCode ===  instanceTypeValue;
    });
    var deviceInfo = {
      volumeSize: instanceTypeSelected.volumeSizeGB,
      numVolumes: instanceTypeSelected.volumeCount,
      mountPoints: null,
      diskIops: 1000
    }
    this.setState({deviceInfo: deviceInfo, volumeType: instanceTypeSelected.volumeType});
  }

  numNodesChanged(value) {
    this.setState({numNodes: value});
  }

  numNodesChangedViaAzList(value) {
    this.setState({numNodes: value, isCustom: true});
  }

  numNodesClicked() {
    this.setState({isCustom: false});
  }

  azChanged(event) {
    this.setState({azCheckState: !this.state.azCheckState});
  }

  softwareVersionChanged(version) {
    this.setState({ybSoftwareVersion: version, isCustom: false});
  }

  replicationFactorChanged(value) {
    var self = this;
    if (!this.props.universe.currentUniverse) {
      this.setState({replicationFactor: value}, function () {
        if (self.state.numNodes <= value) {
          self.setState({numNodes: value, isCustom: false});
        }
      });
    }
  }

  componentWillUpdate(newProps) {
    if (newProps.universe.formSubmitSuccess) {
      this.props.reset();
    }
  }

  componentDidUpdate(prevProps, prevState) {
    if (!_.isEqual(this.state, prevState)
        && prevProps.universe.showModal && this.props.universe.showModal && this.props.universe.visibleModal === "universeModal") {
      if (this.state.numNodes >= this.state.replicationFactor) {
        this.configureUniverseNodeList();
      }
    }
  }

  numVolumesChanged(val) {
    var currentDeviceInfo = this.state.deviceInfo;
    currentDeviceInfo.numVolumes = val;
    this.setState({deviceInfo: currentDeviceInfo});
    this.configureUniverseNodeList("deviceInfo", currentDeviceInfo);
  }

  volumeSizeChanged(val) {
    var currentDeviceInfo = this.state.deviceInfo;
    currentDeviceInfo.volumeSize = val;
    this.setState({deviceInfo: currentDeviceInfo});
    this.configureUniverseNodeList("deviceInfo", currentDeviceInfo);
  }

  diskIopsChanged(val) {
    var currentDeviceInfo = this.state.deviceInfo;
    currentDeviceInfo.diskIops = val;
    this.setState({deviceInfo: currentDeviceInfo});
    this.configureUniverseNodeList("deviceInfo", currentDeviceInfo);
  }

  componentWillReceiveProps(nextProps) {
    var self = this;
    const {universe: {showModal, visibleModal, currentUniverse}} = nextProps;
    if (nextProps.cloud.instanceTypes !== this.props.cloud.instanceTypes
       && isValidArray(nextProps.cloud.instanceTypes) && !isValidArray(Object.keys(this.state.deviceInfo))
       && isValidObject(this.state.instanceTypeSelected)) {
       var instanceTypeSelected = nextProps.cloud.instanceTypes.find(function(item){
        return item.instanceTypeCode ===  self.state.instanceTypeSelected;
      });
      if (isValidObject(instanceTypeSelected) && isValidArray(Object.keys(instanceTypeSelected))) {
        var deviceInfo = {
          volumeSize: instanceTypeSelected.volumeSizeGB,
          numVolumes: instanceTypeSelected.volumeCount,
          mountPoints: null,
          diskIops: 1000,
        }
        this.setState({deviceInfo: deviceInfo, volumeType: instanceTypeSelected.volumeType});
      }
    }
    // Set Default Software Package in case of Create
    if (nextProps.softwareVersions !== this.props.softwareVersions
        && !isValidObject(this.props.universe.currentUniverse) && isValidArray(nextProps.softwareVersions)
        && !isValidArray(this.props.softwareVersions)) {
        this.setState({ybSoftwareVersion: nextProps.softwareVersions[0]});
    }

    // If dialog has been closed and opened again in-case of edit, then repopulate current config
    if (currentUniverse && isValidArray(Object.keys(currentUniverse)) && showModal && !this.props.universe.showModal && visibleModal === "universeModal") {
      var userIntent  = currentUniverse.universeDetails.userIntent;
      this.props.getExistingUniverseConfiguration(currentUniverse.universeDetails);
      var providerUUID = currentUniverse.provider.uuid;
      var isMultiAZ = true;
      if (userIntent && providerUUID) {
        this.setState({
          providerSelected: providerUUID, azCheckState: isMultiAZ, instanceTypeSelected: userIntent.instanceType,
          numNodes: userIntent.numNodes, replicationFactor: userIntent.replicationFactor,
          ybSoftwareVersion: userIntent.ybSoftwareVersion, regionList: userIntent.regionList,
          accessKeyCode: userIntent.accessKeyCode
        });
      }
    }
  }

  render() {
    var self = this;
    const { visible, handleSubmit, title, universe, softwareVersions, cloud, accessKeys } = this.props;
    var universeProviderList = [];
    var currentProviderCode = "";
    if (isValidArray(cloud.providers)) {
      universeProviderList = cloud.providers.map(function(providerItem, idx) {
        if (providerItem.uuid === self.state.providerSelected) {
          currentProviderCode = providerItem.code;
        }
        return <option key={providerItem.uuid} value={providerItem.uuid}>
          {providerItem.name}
        </option>;
      });
    }
    universeProviderList.unshift(<option key="" value=""></option>);

    var universeRegionList = cloud.regions && cloud.regions.map(function (regionItem, idx) {
      return {value: regionItem.uuid, label: regionItem.name};
    });

    var universeInstanceTypeList = <option/>;
    if (currentProviderCode === "aws") {
      var optGroups = this.props.cloud.instanceTypes.reduce(function(groups, it) {
          var prefix = it.instanceTypeCode.substr(0, it.instanceTypeCode.indexOf("."));
          groups[prefix] ? groups[prefix].push(it.instanceTypeCode): groups[prefix] = [it.instanceTypeCode];
          return groups;}
        , {});
      if (isValidArray(Object.keys(optGroups))) {
        universeInstanceTypeList = Object.keys(optGroups).map(function(key, idx){
          return(
            <optgroup label={`${key.toUpperCase()} type instances`} key={key+idx}>
              {
                optGroups[key].sort(function(a, b) { return /\d+(?!\.)/.exec(a) - /\d+(?!\.)/.exec(b) }).map(function(item, arrIdx){
                  return (<option key={idx+arrIdx} value={item}>
                    {item}
                  </option>)
                })
              }
            </optgroup>
          )
        })
      }
    } else {
      universeInstanceTypeList =
        cloud.instanceTypes && cloud.instanceTypes.map(function (instanceTypeItem, idx) {
          return <option key={instanceTypeItem.instanceTypeCode}
                         value={instanceTypeItem.instanceTypeCode}>
            {instanceTypeItem.instanceTypeCode}
          </option>
        });
    }
    if (isValidArray(universeInstanceTypeList)) {
      universeInstanceTypeList.unshift(<option key="" value="">Select</option>);
    }

    var submitLabel, submitAction;
    if (this.props.type === "Create") {
      submitLabel = 'Create';
      submitAction = handleSubmit(this.createUniverse);
    } else {
      submitLabel = 'Save';
      submitAction = handleSubmit(this.editUniverse);
    }

    var configDetailItem = "";
    if (isDefinedNotNull(universe.universeResourceTemplate) && isDefinedNotNull(universe.universeConfigTemplate)) {
      configDetailItem = <UniverseResources resources={universe.universeResourceTemplate} />
    }

    var softwareVersionOptions = softwareVersions.map(function(item, idx){
      return <option key={idx} value={item}>{item}</option>
    })

    var accessKeyOptions = <option key={1} value={this.state.accessKeyCode}>{this.state.accessKeyCode}</option>;
    if (isValidObject(accessKeys) && isValidArray(accessKeys.data)) {
      accessKeyOptions = accessKeys.data.map(function(item, idx){
        return <option key={idx} value={item.idKey.keyCode}>{item.idKey.keyCode}</option>
      })
    }

    var placementStatus = <span/>;
    if (self.props.universe.currentPlacementStatus) {
      placementStatus = <AZPlacementInfo placementInfo={self.props.universe.currentPlacementStatus}/>
    }


    var deviceDetail = null;
    function volumeTypeFormat(num) {
      return num + ' GB';
    }
    var isRFReadOnly = false;
    var isProviderReadOnly = false;
    if (universe.currentUniverse) {
      isRFReadOnly = true;
      isProviderReadOnly = true;
    }

    if (isValidArray(Object.keys(self.state.deviceInfo))) {
      if (self.state.volumeType === 'EBS') {
        deviceDetail = <span className="volume-info">
          <span className="volume-info-field" style={{width: 60}}>
            <Field name="volumeCount" component={YBControlledNumericInput}
                   label="Number of Volumes" val={self.state.deviceInfo.numVolumes} onInputChanged={self.numVolumesChanged}/>
          </span>
          &times;
          <span className="volume-info-field" style={{width: 100}}>
            <Field name="volumeSize" component={YBControlledNumericInput} label="Volume Size" val={self.state.deviceInfo.volumeSize}
                   valueFormat={volumeTypeFormat} onInputChanged={self.volumeSizeChanged}/>
          </span>
          <label className="form-item-label">Provisioned IOPS</label>
          <span className="volume-info-field" style={{width: 80}}>
            <Field name="diskIops" component={YBControlledNumericInput} label="Provisioned IOPS"
                   val={self.state.deviceInfo.diskIops} onInputChanged={self.diskIopsChanged}/>
          </span>
        </span>;
      } else if (self.state.volumeType === 'SSD') {
        deviceDetail = <span className="volume-info">
          {self.state.deviceInfo.numVolumes} &times;&nbsp;
          {volumeTypeFormat(self.state.deviceInfo.volumeSize)} {self.state.volumeType}
          &nbsp;({self.state.deviceInfo.diskIops} Provisioned IOPS)
        </span>;
      }
    }

    return (
      <YBModal visible={visible} onHide={this.hideModal} title={title} error={universe.error}
        submitLabel={submitLabel} showCancelButton={true}
          onFormSubmit={submitAction} formName={"UniverseForm"} footerAccessory={configDetailItem} size="large">
        <Row className={"no-margin-row"}>
        <Col lg={6}>
          <h4>Cloud Configuration</h4>
          <div className="form-right-aligned-labels">
            <Field name="universeName" type="text" component={YBTextInputWithLabel} label="Name"
                   onValueChanged={this.universeNameChanged} isReadOnly={isDefinedNotNull(universe.currentUniverse)} />
            <Field name="provider" type="select" component={YBSelectWithLabel} label="Provider"
                   options={universeProviderList} onInputChanged={this.providerChanged} readOnlySelect={isProviderReadOnly}
            />
            <Field name="regionList" component={YBMultiSelectWithLabel}
                    label="Regions" options={universeRegionList}
                    selectValChanged={this.regionListChanged} multi={this.state.azCheckState}
                    providerSelected={this.state.providerSelected}/>
            <Field name="numNodes" type="text" component={YBControlledNumericInputWithLabel}
                   label="Nodes" onInputChanged={this.numNodesChanged} onLabelClick={this.numNodesClicked} val={this.state.numNodes}
                    minVal={Number(this.state.replicationFactor)}/>
          </div>
        </Col>
        <Col lg={6} className={"universe-az-selector-container"}>
          <AZSelectorTable {...this.props} numNodesChanged={this.numNodesChangedViaAzList} setPlacementInfo={this.setPlacementInfo}/>
          {placementStatus}
        </Col>
        </Row>
        <Row className={"no-margin-row top-border-row"}>
          <Col lg={12}>
            <h4>Instance Configuration</h4>
          </Col>
          <Col lg={4}>
            <div className="form-right-aligned-labels">
              <Field name="instanceType" type="select" component={YBSelectWithLabel} label="Instance Type"
                     options={universeInstanceTypeList}
                     defaultValue={this.state.instanceTypeSelected} onInputChanged={this.instanceTypeChanged}
              />

            </div>
          </Col>
          {deviceDetail &&
            <Col lg={8}>
              <div className="form-right-aligned-labels">
                <div className="form-group universe-form-instance-info">
                  <label className="form-item-label">Volume Info</label>
                  {deviceDetail}
                </div>
              </div>
            </Col>
          }
        </Row>
        <Row className={"no-margin-row top-border-row"}>
          <Col lg={12}>
            <h4>Advanced</h4>
          </Col>
          <Col lg={4}>
            <div className="form-right-aligned-labels">
              <Field name="replicationFactor" type="text" component={YBRadioButtonBarWithLabel} options={[1, 3, 5, 7]}
                     label="Replication Factor" initialValue={this.state.replicationFactor}
                     onSelect={this.replicationFactorChanged} isReadOnly={isRFReadOnly}/>
            </div>
          </Col>
          <Col lg={4}>
            <div className="form-right-aligned-labels">
              <Field name="ybSoftwareVersion" type="select" component={YBSelectWithLabel} defaultValue={this.state.ybSoftwareVersion}
                     options={softwareVersionOptions} label="YugaByte Version" onInputChanged={this.softwareVersionChanged}/>
            </div>
          </Col>
          <Col lg={4}>
            <div className="form-right-aligned-labels">
              <Field name="accessKeyCode" type="select" component={YBSelectWithLabel} label="Access Key"
                     isReadOnly={true} defaultValue={this.state.accessKeyCode} options={accessKeyOptions} />
            </div>
          </Col>
       </Row>
      </YBModal>
    );
  }
}

import React, { Component, PropTypes } from 'react';
import YBInput from '../fields/YBInputField';
import YBSelect from './../fields/YBSelect';
import YBCheckBox from './../fields/YBCheckBox';
import YBMultiSelect from './../fields/YBMultiSelect';
import YBNumericInput from './../fields/YBNumericInput';
import { Field } from 'redux-form';
import YBModal from './../fields/YBModal';
import {isValidObject, isValidArray} from '../../utils/ObjectUtils';
import {Row, Col} from 'react-bootstrap';
import DescriptionItem from '../DescriptionItem';
import YBCost from '../fields/YBCost';


class UniverseConfigDetail extends Component {
  render() {
    const {universe: {universeResourceTemplate}} =  this.props;
    var totalMemory = <span/>;
    var costPerDay =  <span/>;
    var costPerMonth = <span/>;
    var volumeSize = <span/>;
    var numCores = <span/>;
    var volumeCount = <span/>;
    if (isValidObject(universeResourceTemplate) && Object.keys(universeResourceTemplate).length > 0) {
      costPerDay = <YBCost value={universeResourceTemplate.pricePerHour} multiplier={"day"} />
      costPerMonth = <YBCost value={universeResourceTemplate.pricePerHour} multiplier={"month"} />
    }
    if (isValidObject(universeResourceTemplate.volumeSizeGB)) {
      volumeSize = <span>{`${universeResourceTemplate.volumeSizeGB} GB`}</span>
    }
    var previewAZList = <span/>;
    if (isValidObject(universeResourceTemplate.azList)) {
      previewAZList = universeResourceTemplate.azList.map(function(item, idx){
        return (
          <span className="config-az-item" key={`config-az-item${idx}`}>{item}
            <span>{idx < (universeResourceTemplate.azList.length - 1) ? ", " : ". "}</span>
        </span>
        )
      });
    }
    if (isValidObject(universeResourceTemplate.volumeCount)) {
      volumeCount = <span>{universeResourceTemplate.volumeCount}</span>
    }
    if (isValidObject(universeResourceTemplate.numCores)) {
      numCores = <span>{universeResourceTemplate.numCores}</span>
    }
    if (isValidObject(universeResourceTemplate.memSizeGB)) {
      totalMemory = <span>{`${universeResourceTemplate.memSizeGB} GB`}</span>
    }
    return (
      <div className="universe-resource-preview">
        <Row>
          <div className="config-main-heading">
              Universe Overview
          </div>
        </Row>
        <Row className="preview-row az-preview-row">
          <Col lg={4}>
            <span className="config-label">AZ Placement</span>
          </Col>
          <Col lg={8}>
            <div className="config-display">{previewAZList}</div>
          </Col>
        </Row>
        <Row className="preview-row">
          <Col lg={6} className="preview-item-half-left">
            <DescriptionItem title="Cores">
              {numCores}
            </DescriptionItem>
          </Col>
          <Col lg={6}>
            <DescriptionItem title="Memory">
              {totalMemory}
            </DescriptionItem>
          </Col>
        </Row>
        <Row className="preview-row">
          <Col lg={6} className="preview-item-half-left">
            <DescriptionItem title="Disk Storage">
              {volumeSize}
            </DescriptionItem>
          </Col>
          <Col lg={6}>
            <DescriptionItem title="Volumes">
              {volumeCount}
            </DescriptionItem>
          </Col>
        </Row>
        <div className="config-main-heading">
          Pricing
        </div>
        <Row className="preview-row cost-preview-row">
          <Col lg={4}>
            <span className="config-label">Price Per Day</span>
          </Col>
          <Col lg={6}>
            <span className="config-price-display">{costPerDay}</span>
          </Col>
        </Row>
        <Row className="preview-row cost-preview-row">
          <Col lg={4}>
            <span className="config-label">Price Per Month</span>
          </Col>
          <Col lg={6}>
            <span className="config-price-display">{costPerMonth}</span>
          </Col>
        </Row>
      </div>
    )
  }
}
export default class UniverseForm extends Component {
  static propTypes = {
    type: PropTypes.oneOf(['Edit', 'Create']).isRequired,
  }

  constructor(props) {
    super(props);
    this.providerChanged = this.providerChanged.bind(this);
    this.regionListChanged = this.regionListChanged.bind(this);
    this.instanceTypeChanged = this.instanceTypeChanged.bind(this);
    this.numNodesChanged = this.numNodesChanged.bind(this);
    this.createUniverse = this.createUniverse.bind(this);
    this.editUniverse = this.editUniverse.bind(this);
    this.universeNameChanged = this.universeNameChanged.bind(this);
    this.azChanged = this.azChanged.bind(this);
    var azInitState = true;
    this.configureUniverseNodeList = this.configureUniverseNodeList.bind(this);
    if (isValidObject(this.props.universe.currentUniverse)) {
      azInitState = this.props.universe.currentUniverse.universeDetails.userIntent.isMultiAZ
    }
    this.state = { instanceTypeSelected: 'm3.medium',
                    azCheckState: azInitState, providerSelected: '' };
  }

  configureUniverseNodeList(fieldName, fieldVal) {
    const {universe: {universeConfigTemplate, currentUniverse}, formValues} = this.props;
    var universeTaskParams = universeConfigTemplate;

    if (isValidObject(currentUniverse)) {
      universeTaskParams.universeUUID = currentUniverse.universeUUID;
      universeTaskParams.expectedUniverseVersion = currentUniverse.version;
    }
    var formSubmitVals = formValues;
    delete formSubmitVals.formType;
    universeTaskParams.userIntent = formSubmitVals;
    universeTaskParams.userIntent[fieldName] = fieldVal;
    if(isValidObject(formValues.instanceType) && isValidArray(universeTaskParams.userIntent.regionList)) {
      this.props.cloud.providers.forEach(function(providerItem, idx){
        if (providerItem.uuid === universeTaskParams.userIntent.provider) {
          universeTaskParams.userIntent.providerType = providerItem.code;
        }
      });
      if (!isValidArray(universeTaskParams.userIntent.regionList)) {
        universeTaskParams.userIntent.regionList = [formSubmitVals.regionList.value];
      } else {
        universeTaskParams.userIntent.regionList = formSubmitVals.regionList.map(function (item, idx) {
          return item.value;
        });
      }
      this.props.submitConfigureUniverse(universeTaskParams);
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
    if(this.props.type === "Edit") {
      var providerUUID = this.props.universe.currentUniverse.provider.uuid;
      var isMultiAZ = this.props.universe.currentUniverse.universeDetails.userIntent.isMultiAZ;
      this.setState({providerSelected: providerUUID});
      this.setState({azCheckState: isMultiAZ});
      this.setState({instanceTypeSelected: this.props.universe.currentUniverse.universeDetails.userIntent.instanceType});
      this.props.getRegionListItems(providerUUID, isMultiAZ);
      this.props.getInstanceTypeListItems(providerUUID);
    }
  }

  universeNameChanged(universeName) {
    this.configureUniverseNodeList("universeName", universeName);
  }

  providerChanged(value) {
    var providerUUID = value;
    this.setState({providerSelected: providerUUID});
    this.props.getRegionListItems(providerUUID, this.state.azCheckState);
    this.props.getInstanceTypeListItems(providerUUID);
  }

  regionListChanged(value) {
    this.configureUniverseNodeList("regionList", value);
  }

  instanceTypeChanged(instanceTypeValue) {
    this.setState({instanceTypeSelected: instanceTypeValue});
    this.configureUniverseNodeList("instanceType", instanceTypeValue);
  }

  numNodesChanged(value) {
    this.setState({numNodes: value});
    this.configureUniverseNodeList("numNodes", value);
  }

  azChanged(event) {
    this.setState({azCheckState: !this.state.azCheckState});
    this.configureUniverseNodeList("isMultiAZ", !JSON.parse(event.target.value));
  }
  componentDidUpdate(newProps) {
    if (newProps.universe.formSubmitSuccess) {
      this.props.reset();
    }
  }
  render() {
    var self = this;
    const { visible, onHide, handleSubmit, title, universe} = this.props;

    var universeProviderList = this.props.cloud.providers.map(function(providerItem, idx) {
      return <option key={providerItem.uuid} value={providerItem.uuid}>
        {providerItem.name}
      </option>;
    });
    universeProviderList.unshift(<option key="" value=""></option>);
    var universeRegionList = this.props.cloud.regions.map(function (regionItem, idx) {
      return {value: regionItem.uuid, label: regionItem.name};
    });
    var universeInstanceTypeList =
      this.props.cloud.instanceTypes.map(function (instanceTypeItem, idx) {
        return <option key={instanceTypeItem.instanceTypeCode}
                       value={instanceTypeItem.instanceTypeCode}>
          {instanceTypeItem.instanceTypeCode}
        </option>
      });
    if(universeInstanceTypeList.length > 0) {
      universeInstanceTypeList.unshift(<option key="" value="">Select</option>);
    }
    var submitAction = this.props.type==="Create" ? handleSubmit(this.createUniverse) :
      handleSubmit(this.editUniverse);

    var configDetailItem = "";
    if (isValidObject(universe.universeResourceTemplate) && isValidObject(universe.universeConfigTemplate)) {
      configDetailItem = <UniverseConfigDetail universe={universe}/>
    }
    // Hide modal when close is clicked, it also resets the form state and sets it to pristine
    var hideModal = function() {
      self.props.reset();
      onHide();
    }
    var isInputReadOnly = false;
    if (isValidObject(universe.currentUniverse)) {
      isInputReadOnly = true;
    }
    return (
           <YBModal visible={visible}
                    onHide={hideModal} title={title} onFormSubmit={submitAction} formName={"UniverseForm"} size="large">
             <Col lg={6}>
              <Field name="universeName" type="text" component={YBInput} label="Universe Name"
                     onValueChanged={this.universeNameChanged} isReadOnly={isInputReadOnly}
              />
              <Field name="provider" type="select" component={YBSelect} label="Provider"
                     options={universeProviderList} onSelectChange={this.providerChanged}
              />
               <Field name="regionList" component={YBMultiSelect}
                      label="Regions" options={universeRegionList}
                      selectValChanged={this.regionListChanged} multi={this.state.azCheckState}
                      providerSelected={this.state.providerSelected}/>
               <Field name="numNodes" type="text" component={YBNumericInput}
                     label="Number Of Nodes" onValueChanged={this.numNodesChanged}/>
              <div className="universeFormSplit">
                Advanced
              </div>
              <Field name="isMultiAZ" type="checkbox" component={YBCheckBox}
                     label="Multi AZ" onClick={this.azChanged}/>
              <Field name="instanceType" type="select" component={YBSelect} label="Instance Type"
                     options={universeInstanceTypeList}
                     defaultValue={this.state.instanceTypeSelected} onSelectChange={this.instanceTypeChanged}
              />
              <Field name="ybServerPackage" type="text" component={YBInput}
                     label="Server Package" defaultValue={this.state.ybServerPackage} />
             </Col>
             <Col lg={6}>
               {configDetailItem}
             </Col>
           </YBModal>
    )
  }
}

UniverseForm.propTypes = {
  "title": PropTypes.string.isRequired
}

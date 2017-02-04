import React, { Component, PropTypes } from 'react';
import { Col } from 'react-bootstrap';
import { Field } from 'redux-form';

import { isDefinedNotNull, isValidArray } from 'utils/ObjectUtils';
import { YBModal, YBCheckBox, YBTextInputWithLabel, YBNumericInputWithLabel, YBSelectWithLabel, 
  YBMultiSelectWithLabel, YBRadioButtonBarWithLabel } from 'components/common/forms/fields';

import AZSelectorTable from './AZSelectorTable';
import UniverseConfigDetail from './UniverseConfigDetail';
import './UniverseForm.scss';

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
    this.universeNameChanged = this.universeNameChanged.bind(this);
    this.serverPackageChanged = this.serverPackageChanged.bind(this);
    this.azChanged = this.azChanged.bind(this);
    var azInitState = true;
    this.configureUniverseNodeList = this.configureUniverseNodeList.bind(this);
    if (isDefinedNotNull(this.props.universe.currentUniverse)) {
      azInitState = this.props.universe.currentUniverse.universeDetails.userIntent.isMultiAZ
    }
    this.state = {
      instanceTypeSelected: 'm3.medium',
      azCheckState: azInitState,
      providerSelected: '',
    };
  }

  configureUniverseNodeList(fieldName, fieldVal) {
    const {universe: {universeConfigTemplate, currentUniverse}, formValues} = this.props;
    var universeTaskParams = universeConfigTemplate;

    if (isDefinedNotNull(currentUniverse)) {
      universeTaskParams.universeUUID = currentUniverse.universeUUID;
      universeTaskParams.expectedUniverseVersion = currentUniverse.version;
    }
    var formSubmitVals = formValues;
    delete formSubmitVals.formType;
    universeTaskParams.userIntent = formSubmitVals;
    universeTaskParams.userIntent[fieldName] = fieldVal;
    if (isDefinedNotNull(formValues.instanceType) && isValidArray(universeTaskParams.userIntent.regionList)) {
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
    if (this.props.type === "Edit") {
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

  serverPackageChanged(packageName) {
    this.configureUniverseNodeList("ybServerPackage", packageName);
  }

  componentDidUpdate(newProps) {
    if (newProps.universe.formSubmitSuccess) {
      this.props.reset();
    }
  }

  render() {
    var self = this;
    const { visible, onHide, handleSubmit, title, universe } = this.props;

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
    if (universeInstanceTypeList.length > 0) {
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
      configDetailItem = <UniverseConfigDetail universe={universe}/>
    }

    // Hide modal when close is clicked, it also resets the form state and sets it to pristine
    var hideModal = function() {
      self.props.reset();
      onHide();
    }

    return (
      <YBModal visible={visible} onHide={hideModal} title={title} error={universe.error}
        submitLabel={submitLabel} showCancelButton={true}
          onFormSubmit={submitAction} formName={"UniverseForm"} footerAccessory={configDetailItem} size="large">
        <Col lg={6}>
          <h4>General Info</h4>
          <div className="form-right-aligned-labels">
            <Field name="universeName" type="text" component={YBTextInputWithLabel} label="Name"
                   onValueChanged={this.universeNameChanged} isReadOnly={isDefinedNotNull(universe.currentUniverse)} />
            <Field name="provider" type="select" component={YBSelectWithLabel} label="Provider"
                   options={universeProviderList} onSelectChange={this.providerChanged}
            />
            <Field name="regionList" component={YBMultiSelectWithLabel}
                    label="Regions" options={universeRegionList}
                    selectValChanged={this.regionListChanged} multi={this.state.azCheckState}
                    providerSelected={this.state.providerSelected}/>
            <Field name="numNodes" type="text" component={YBNumericInputWithLabel}
                   label="Nodes" onValueChanged={this.numNodesChanged}/>
            <Field name="instanceType" type="select" component={YBSelectWithLabel} label="Instance Type"
                   options={universeInstanceTypeList}
                   defaultValue={this.state.instanceTypeSelected} onSelectChange={this.instanceTypeChanged}
            />
          </div>
          <h4>Advanced</h4>
          <div className="form-right-aligned-labels">
            <Field name="replicationFactor" type="text" component={YBRadioButtonBarWithLabel} options={[1, 3, 5, 7]}
                   label="Replication Factor" />
            <Field name="ybServerPackage" type="text" component={YBTextInputWithLabel}
                   label="Server Package" defaultValue={this.state.ybServerPackage}
                   onValueChanged={this.serverPackageChanged} />
          </div>
        </Col>
        <Col lg={6}>
          <h4>Availability Zones</h4>
          <Field name="isMultiAZ" type="checkbox" component={YBCheckBox}
            label="Multi AZ" onClick={this.azChanged}/>
          <AZSelectorTable universe={universe} />
        </Col>
      </YBModal>
    );
  }
}

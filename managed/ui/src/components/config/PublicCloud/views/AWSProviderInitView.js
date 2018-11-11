// Copyright (c) YugaByte, Inc.

import React, { Fragment,  Component } from 'react';
import { Row, Col, Alert } from 'react-bootstrap';
import { YBInputField, YBTextInputWithLabel, YBControlledSelectWithLabel, YBSelectWithLabel, YBToggle, YBAddRowButton, YBButton } from 'components/common/forms/fields';

import { FlexContainer, FlexGrow, FlexShrink } from '../../../common/flexbox/YBFlexBox';
import { isDefinedNotNull, isNonEmptyString, isValidObject, trimString } from 'utils/ObjectUtils';
import { reduxForm, formValueSelector, change, FieldArray, Field, getFormValues } from 'redux-form';
import { connect } from 'react-redux';
import AddRegionPopupForm from './AddRegionPopupForm';
import _ from 'lodash';


const regionsData = 
  [{
    destVpcRegion: 'ap-northeast-1',
    zones: ['ap-northeast-1a', 'ap-northeast-1c', 'ap-northeast-1d']
  },
  {
    destVpcRegion: 'ap-northeast-2',
    zones: ['ap-northeast-2a', 'ap-northeast-2c']
  },
  {
    destVpcRegion: 'ap-south-1',
    zones: ['ap-south-1a', 'ap-south-1b']
  },
  {
    destVpcRegion: 'ap-southeast-1',
    zones: ['ap-southeast-1a', 'ap-southeast-1b', 'ap-southeast-1c']
  },
  {
    destVpcRegion: 'ap-southeast-2',
    zones: ['ap-southeast-2a', 'ap-southeast-2b', 'ap-southeast-2c']
  },
  {
    destVpcRegion: 'ca-central-1',
    zones: ['ca-central-1a', 'ca-central-1b']
  },
  {
    destVpcRegion: 'eu-central-1',
    zones: ['eu-central-1a', 'eu-central-1b', 'eu-central-1c']
  },
  {
    destVpcRegion: 'eu-west-1',
    zones: ['eu-west-1a', 'eu-west-1b', 'eu-west-1c']
  },
  {
    destVpcRegion: 'eu-west-2',
    zones: ['eu-west-2a', 'eu-west-2b', 'eu-west-2c']
  },
  {
    destVpcRegion: 'eu-west-3',
    zones: ['eu-west-3a', 'eu-west-3b', 'eu-west-3c']
  },
  {
    destVpcRegion: 'sa-east-1',
    zones: ['sa-east-1a', 'sa-east-1b', 'sa-east-1c']
  },
  {
    destVpcRegion: 'us-east-1',
    zones: ['us-east-1a', 'us-east-1b', 'us-east-1c', 'us-east-1d', 'us-east-1e', 'us-east-1f']
  },
  {
    destVpcRegion: 'us-east-2',
    zones: ['us-east-2a', 'us-east-2b', 'us-east-2c']
  },
  {
    destVpcRegion: 'us-west-1',
    zones: ['us-west-1a', 'us-west-1b']
  },
  {
    destVpcRegion: 'us-west-2',
    zones: ['us-west-2a', 'us-west-2b', 'us-west-2c']
  }];

const renderAZMapping = ({ fields, meta: { touched, error, submitFailed }, networkSetupType }) => {
  return 0;
};

const validationIsRequired = value => value && value.trim() !== '' ? undefined : 'Required';

class AZInput extends Component {
  render() {
    const {deleteRow, item, zones, zonesAdded, index} = this.props;
    const zonesAvailable = zones.filter(zone => zonesAdded.filter(e => e.zone).length ? zonesAdded.findIndex((element, index, array) => element.zone === zone) < 0 : true);

    const options = [<option key={0} value="">Select zone</option>, ...zones.map((zone, idx) => <option key={idx+1} disabled={!(zonesAvailable.indexOf(zone)>-1 || zone === zonesAdded[index].zone)} value={zone}>{zone}</option>)];
    return (
      <FlexContainer>
        <FlexGrow>
          <Row>
            <Col lg={6}>
              <Field name={`${item}.zone`} validate={validationIsRequired} component={YBControlledSelectWithLabel} defaultValue={zonesAdded[index].zone} options={options}/>
            </Col>
            <Col lg={6}>
              <Field name={`${item}.subnet`} validate={validationIsRequired} component={YBInputField} placeHolder="Subnet ID"/>
            </Col>
          </Row>
        </FlexGrow>
        <FlexShrink>
          <i className="fa fa-times fa-fw delete-row-btn" onClick={deleteRow}/>
        </FlexShrink>
      </FlexContainer>
    );
  }
}

class renderAZMappingForm extends Component {
  
  componentWillMount() {
    if (this.props.fields.length === 0) {
      this.props.fields.push({});
    }
  }
  componentWillReceiveProps(nextProps) {
    const { zones } = this.props;
    if (!_.isEqual(zones, nextProps.zones)) {
      this.props.fields.removeAll();
      this.props.fields.push({});
    }
  }
  render() {
    const { fields, zones, regionFormData } = this.props;
    const addFlagItem = function() {
      fields.push({});
    };

    const zonesAdded = regionFormData && regionFormData.azToSubnetIds;
    const azFieldList = fields.map((item, idx) => (
      <AZInput item={item} key={idx} zones={zones} index={idx} zonesAdded={zonesAdded} deleteRow={() => fields.getAll().length > 1 ? fields.remove(idx) :  null} />
    ));
    return (
      <Fragment>
        <div className="divider"></div>
        <h5>AZ mapping</h5>
        <div className="form-field-grid">
          {
            azFieldList
          }
          {
            fields.length < zones.length &&

            <YBAddRowButton 
            btnText="Add zone" 
            onClick={addFlagItem} />
          }
        </div>
      </Fragment>
    );
  }
}

const renderAZMappingFormContainer = connect(
  state => ({
    regionFormData: getFormValues('addRegionConfig')(state)
  })
)(renderAZMappingForm);


class renderRegions extends Component {
  constructor(props) {
    super(props);
    this.state = {
      regionName: undefined,
      editRegionIndex: undefined
    };
  }

  closeModal = () => {
    this.props.closeModal();
    this.setState({
      regionName: undefined,
      editRegionIndex: undefined
    });
  }

  render() {
    const { fields, networkSetupType, modal, showModal, updateFormField, formRegions } = this.props;
    const self = this;

    const optionsRegion = [<option key={0} value="">Select region</option>, 
      ...( this.state.editRegionIndex === undefined 
        //if add new flow - remove already added regions from region select picker 
        ? _.differenceBy(regionsData, formRegions, 'destVpcRegion')
          .map((region, index) => <option key={index+1} value={region.destVpcRegion}>{region.destVpcRegion}</option>)

        //if edit flow - remove already added regions from region select picker except one to edit and mark it selected
        : _.differenceBy(regionsData, _.filter(formRegions, (o) => o.destVpcRegion !== formRegions[self.state.editRegionIndex].destVpcRegion), 'destVpcRegion')
          .map((region, index) => <option key={index+1} value={region.destVpcRegion}>{region.destVpcRegion}</option>)
      )];
    
    //depending on selected region fetch zones matching this region 
    const optionsZones = (self.state.regionName && _.find(regionsData, function(o) { return o.destVpcRegion === self.state.regionName; }).zones) || [];
    
    return (
      <Row>
        <Col lg={10}>
          <h4 className="regions-form-title">Regions</h4>

          {/* Add/Edit region modal logic */}

          {modal.showModal && modal.visibleModal === "addRegionConfig" &&
            <AddRegionPopupForm visible={true} submitLabel={'Add region'} 
              //pass region object to edit flow or undefined
              editRegion = {
                this.state.editRegionIndex !== undefined
                ? formRegions[this.state.editRegionIndex]
                : undefined} 
              
              showCancelButton={true}

              title = { networkSetupType==="new_vpc"
                ? "Add new region"
                : "Specify region info" } 

              onFormSubmit={(values) => {
                if (values.azToSubnetIds) values.azToSubnetIds.sort((a, b) => a.zone > b.zone ? 1 : -1);
                if (this.state.editRegionIndex !== undefined) {
                  // update region if edit flow 
                  updateFormField(`regionList[${this.state.editRegionIndex}]`, values);
                } else {
                  // push new region object if add flow 
                  fields.push(values);
                }
                // close modal
                this.closeModal();
              }} 

              onHide={this.closeModal}>

              <Field 
                name={`destVpcRegion`}
                label="Region"
                validate={validationIsRequired}
                component={YBControlledSelectWithLabel}
                defaultValue={
                  this.state.editRegionIndex !== undefined &&
                  formRegions.length &&
                  formRegions[this.state.editRegionIndex].destVpcRegion
                } 
                options={optionsRegion}
                onInputChanged={
                  (event)=>{
                    this.setState({regionName:event.target.value});
                  }
                }
              />
              
              {networkSetupType === "new_vpc" ? 

                // New region fields
                <Fragment>
                  <Field name={`vpcCidr`} type="text" component={YBTextInputWithLabel} label={'VPC CIDR (optional)'}
                        normalize={trimString} />
                  <Field name={`customImageId`} type="text" label={'Custom AMI ID (optional)'} component={YBTextInputWithLabel}
                        normalize={trimString} />
                </Fragment>
              : 

                // Specify existing region fields
                <Fragment>
                  <Field name={`destVpcId`} type="text" validate={validationIsRequired} component={YBTextInputWithLabel} label={'VPC ID'}
                        normalize={trimString} />
                  <Field name={`customSecurityGroupId`} validate={validationIsRequired} type="text" label={'Security Group ID'} component={YBTextInputWithLabel}
                        normalize={trimString} />
                  <Field name={`customImageId`} type="text" label={'Custom AMI ID (optional)'} component={YBTextInputWithLabel}
                        normalize={trimString} />
                  <FieldArray name={`azToSubnetIds`} component={renderAZMappingFormContainer} zones={optionsZones} region={this.state.editRegionIndex !== undefined && formRegions[this.state.editRegionIndex]}/>
                </Fragment>
              }
            </AddRegionPopupForm>
          }

          {/* Render list of added regions */}
          <ul className="config-region-list">

            {/* If there're any render regions table header */}
            {fields.length > 0 && 
            <li className="header-row">
              {networkSetupType==="new_vpc" ?
                <Fragment>
                  <div>Name</div>
                  <div>VPC CIDR</div>
                  <div>Custom AMI</div>
                  <div></div>
                </Fragment>
              :
                <Fragment>
                  <div>Name</div>
                  <div>VPC ID</div>
                  <div>Security group</div>
                  <div>Zones</div>
                  <div></div>
                </Fragment>
              }
            </li>}

            {/* Render regions table itself */}
            {fields.map((region, index) => {
              return (
                <li key={index} onClick={() => {
                  // Regions edit popup handler 
                  showModal("addRegionConfig");
                  this.setState({
                    regionName: formRegions[index].destVpcRegion,
                    editRegionIndex: index
                  });
                }}>
                  {networkSetupType==="new_vpc" ?

                    // New region fields 
                    <Fragment>
                      <div>
                        <Field name={`${region}.destVpcRegion`}  type="text" component={YBTextInputWithLabel} isReadOnly={true}
                        normalize={trimString} />
                      </div>
                      <div>
                        <Field name={`${region}.vpcCidr`}  type="text" component={YBTextInputWithLabel} isReadOnly={true}
                        normalize={trimString} />
                      </div>
                      <div>
                        <Field name={`${region}.customImageId`}  type="text" component={YBTextInputWithLabel} isReadOnly={true}
                          normalize={trimString} />
                      </div>
                      <div>
                        <button
                          type="button"
                          className="delete-provider"
                          onClick={(e) => {fields.remove(index); e.stopPropagation();}}
                        ><i className="fa fa-times fa-fw delete-row-btn" /></button>
                      </div>
                    </Fragment>
                  :
                    <Fragment>
                      <div>
                        <Field name={`${region}.destVpcRegion`}  type="text" component={YBTextInputWithLabel} isReadOnly={true}
                        normalize={trimString} />
                      </div>
                      <div>
                        <Field name={`${region}.destVpcId`} type="text" component={YBTextInputWithLabel} isReadOnly={true}
                        normalize={trimString} />
                      </div>
                      <div>
                        <Field name={`${region}.customSecurityGroupId`} type="text"  component={YBTextInputWithLabel} isReadOnly={true}
                        normalize={trimString} />
                      </div>
                      <div>
                        {formRegions && formRegions[index].azToSubnetIds && (formRegions[index].azToSubnetIds.length + (formRegions[index].azToSubnetIds.length>1 ? ' zones' : ' zone'))}
                      </div>
                      <div>
                        <button
                          type="button"
                          className="delete-provider"
                          onClick={(e) => {fields.remove(index); e.stopPropagation();}}
                        ><i className="fa fa-times fa-fw delete-row-btn" /></button>
                      </div>
                    </Fragment>
                  }
                  {false && <FieldArray name={`${region}.azToSubnetIds`} component={renderAZMapping} />}
                </li>);
            })}
          </ul>
          <button type="button" className="btn btn-default btn-add-region" onClick={() => showModal("addRegionConfig")}><div className="btn-icon"><i className="fa fa-plus"></i></div>Add region</button>
        </Col>
      </Row>
    );
  }
}

class AWSProviderInitView extends Component {
  constructor(props) {
    super(props);
    this.state = {
      networkSetupType: "new_vpc",
      setupHostedZone: false,
      credentialInputType: "custom_keys",
      keypairsInputType: "yw_keypairs"
    };
  }

  networkSetupChanged = (value) => {
    this.updateFormField("regionList", null);
    this.setState({networkSetupType: value});
  }

  credentialInputChanged = (value) => {
    this.setState({credentialInputType: value});
  }

  keypairsInputChanged = (value) => {
    this.setState({keypairsInputType: value});
  }

  updateFormField = (field, value) => {
    this.props.dispatch(change("awsProviderConfigForm", field, value));
  };

  createProviderConfig = formValues => {
    const {hostInfo} = this.props;
    const awsProviderConfig = {};
    if (this.state.credentialInputType === "custom_keys") {
      awsProviderConfig['AWS_ACCESS_KEY_ID'] = formValues.accessKey;
      awsProviderConfig['AWS_SECRET_ACCESS_KEY'] = formValues.secretKey;
    };
    if (isDefinedNotNull(formValues.hostedZoneId)) {
      awsProviderConfig['AWS_HOSTED_ZONE_ID'] = formValues.hostedZoneId;
    }
    const regionFormVals = {};
    if (this.isHostInAWS()) {
      const awsHostInfo = hostInfo["aws"];
      regionFormVals["hostVpcRegion"] = awsHostInfo["region"];
      regionFormVals["hostVpcId"] = awsHostInfo["vpc-id"];
    }
    
    const perRegionMetadata = {};
    if (this.state.networkSetupType !== "new_vpc") {
      formValues.regionList && formValues.regionList.forEach((item) => perRegionMetadata[item.destVpcRegion] = {
        "vpcId": item.destVpcId,
        "azToSubnetIds": item.azToSubnetIds.reduce(
          (map, obj)=> {
            map[obj.zone] = obj.subnet;
            return map;
          }, {}),
        "customImageId": item.customImageId,
        "customSecurityGroupId": item.customSecurityGroupId
      });
    } else {
      formValues.regionList && formValues.regionList.forEach((item) => perRegionMetadata[item.destVpcRegion] = {
        "vpcCidr": item.vpcCidr,
        "customImageId": item.customImageId
      });
    }

    if (this.state.keypairsInputType === "custom_keypairs") {
      regionFormVals["keyPairName"] = formValues.keyPairName;
      regionFormVals["sshPrivateKeyContent"] = formValues.sshPrivateKeyContent;
      regionFormVals["sshUser"] = formValues.sshUser;
    }
    regionFormVals["perRegionMetadata"] = perRegionMetadata;
    this.props.createAWSProvider(formValues.accountName, awsProviderConfig, regionFormVals);
  };

  isHostInAWS = () => {
    const { hostInfo } = this.props;
    return isValidObject(hostInfo) && isValidObject(hostInfo["aws"]) &&
      hostInfo["aws"]["error"] === undefined;
  }

  hostedZoneToggled = (event) => {
    this.setState({setupHostedZone: event.target.checked});
  }

  closeModal = () => {
    this.props.closeModal();
  }

  generateRow = (label, field) => {
    return (
      <Row className="config-provider-row">
        <Col lg={3}>
          <div className="form-item-custom-label">
            {label}
          </div>
        </Col>
        <Col lg={7}>
          {field}
        </Col>
      </Row>
    );
  }

  regionsSection = (formRegions) => {
    return (
      <Fragment>
        <FieldArray name="regionList" dispatch={this.props.dispatch} formRegions={formRegions} component={renderRegions} updateFormField={this.updateFormField} networkSetupType={this.state.networkSetupType} modal={this.props.modal} showModal={this.props.showModal} closeModal={this.props.closeModal} />
      </Fragment>
    );
  };

  render() {
    const { handleSubmit, submitting, error, formRegions } = this.props;
    const network_setup_options = [
      <option key={1} value={"new_vpc"}>{"Create a new VPC"}</option>,
      <option key={2} value={"existing_vpc"}>{"Specify an existing VPC"}</option>
    ];
    if (this.isHostInAWS()) {
      network_setup_options.push(
        <option key={3} value={"host_vpc"}>{"Use VPC of the Admin Console instance"}</option>
      );
    }

    const divider = <Row><Col lg={10}><div className="divider"></div></Col></Row>;

    const regionsSection = this.regionsSection(formRegions);

    let hostedZoneField = <span />;
    if (this.state.setupHostedZone) {
      hostedZoneField = this.generateRow("Route 53 Zone ID",
        <Field name="hostedZoneId" type="text" component={YBTextInputWithLabel}
          normalize={trimString} />
      );
    }
    const credential_input_options = [
      <option key={1} value={"custom_keys"}>{"Input Access and Secret keys"}</option>,
      <option key={2} value={"local_iam_role"}>{"Use IAM Role on instance"}</option>
    ];
    const keypair_input_options = [
      <option key={1} value={"yw_keypairs"}>{"Allow YW to manage keypairs"}</option>,
      <option key={2} value={"custom_keypairs"}>{"Provide custom KeyPair information"}</option>
    ];
    let customKeyFields = <span />;
    if (this.state.credentialInputType === "custom_keys") {
      const accessKeyField = (
        <Field name="accessKey" type="text" component={YBTextInputWithLabel}
          normalize={trimString} />);
      const secretKeyField = (
        <Field name="secretKey" type="text" component={YBTextInputWithLabel}
          normalize={trimString} />);
      customKeyFields = (
        <Fragment>
          {this.generateRow("Access Key ID", accessKeyField)}
          {this.generateRow("Secret Access Key", secretKeyField)}
        </Fragment>
      );
    }

    let customKeypairsFields = <span />;
    if (this.state.keypairsInputType === "custom_keypairs") {
      const keypairsNameField = (
        <Field name="keyPairName" type="text" component={YBTextInputWithLabel}
          normalize={trimString} />);
      const keypairsPemFileField = (
        <Field name="sshPrivateKeyContent" type="text" component={YBTextInputWithLabel}
          normalize={trimString} />);
      const keypairsSshField = (
        <Field name="sshUser" type="text" component={YBTextInputWithLabel}
          normalize={trimString} />);
      customKeypairsFields = (
        <Fragment>
          {this.generateRow("Keypair Name", keypairsNameField)}
          {this.generateRow("PEM File Content", keypairsPemFileField)}
          {this.generateRow("SSH User", keypairsSshField)}
        </Fragment>
      );
    }

    const nameField = this.generateRow(
      "Name",
      <Field name="accountName" type="text" component={YBTextInputWithLabel} />);
    const credentialInputField = this.generateRow(
      "Credential Type",
      <Field name="credential_input" component={YBSelectWithLabel}
        options={credential_input_options} onInputChanged={this.credentialInputChanged} />
    );

    const keypairsInputField = this.generateRow(
      "Keypairs Management",
      <Field name="keypairs_input" component={YBSelectWithLabel}
        options={keypair_input_options} onInputChanged={this.keypairsInputChanged} />
    );
    const networkInputField = this.generateRow(
      "VPC Setup",
      <Field name="network_setup" component={YBSelectWithLabel} options={network_setup_options}
        onInputChanged={this.networkSetupChanged} />);
    const hostedZoneToggleField = this.generateRow(
      "Enable Hosted Zone",
      <Field name="setupHostedZone" component={YBToggle}
        defaultChecked={this.state.setupHostedZone} onToggle={this.hostedZoneToggled} />);
    return (
      <div className="provider-config-container">
        <form name="awsProviderConfigForm" onSubmit={handleSubmit(this.createProviderConfig)}>
          <div className="editor-container">
            <Row className="config-section-header">
              <Col lg={8}>
                {error && <Alert bsStyle="danger">{error}</Alert>}
                {nameField}
                {divider}
                {credentialInputField}
                {customKeyFields}
                {divider}
                {keypairsInputField}
                {customKeypairsFields}
                {divider}
                {hostedZoneToggleField}
                {hostedZoneField}
                {divider}
                {networkInputField}
                {regionsSection}
              </Col>
            </Row>
          </div>
          <div className="form-action-button-container">
            <YBButton btnText={"Save"} btnClass={"btn btn-default save-btn"}
                      disabled={submitting } btnType="submit"/>
          </div>
        </form>
      </div>
    );
  }
}

function validate(values) {
  const errors = {};
  if (!isNonEmptyString(values.accountName)) {
    errors.accountName = 'Account Name is required';
  }

  if (!values.accessKey || values.accessKey.trim() === '') {
    errors.accessKey = 'Access Key is required';
  }

  if(!values.secretKey || values.secretKey.trim() === '') {
    errors.secretKey = 'Secret Key is required';
  }

  if (values.network_setup === "existing_vpc") {
    if (!isNonEmptyString(values.destVpcId)) {
      errors.destVpcId = 'VPC ID is required';
    }
    if (!isNonEmptyString(values.destVpcRegion)) {
      errors.destVpcRegion = 'VPC region is required';
    }
  }

  if (values.setupHostedZone && !isNonEmptyString(values.hostedZoneId)) {
    errors.hostedZoneId = 'Route53 Zone ID is required';
  }
  return errors;
}

let awsProviderConfigForm = reduxForm({
  form: 'awsProviderConfigForm',
  validate
})(AWSProviderInitView);

// Decorate with connect to read form values
const selector = formValueSelector('awsProviderConfigForm'); // <-- same as form name
awsProviderConfigForm = connect(state => {
  // can select values individually
  const formRegions = selector(state, 'regionList');
  return {
    formRegions
  };
})(awsProviderConfigForm);

export default awsProviderConfigForm;

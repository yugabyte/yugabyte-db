// Copyright (c) YugaByte, Inc.

import { Component, Fragment } from 'react';
import { connect } from 'react-redux';
import { Row, Col } from 'react-bootstrap';
import { toast } from 'react-toastify';
import {
  YBButton,
  YBAddRowButton,
  YBToggle,
  YBNumericInput,
  YBTextInputWithLabel,
  YBSelectWithLabel,
  YBDropZone,
  YBInputField
} from '../../../common/forms/fields';

import { change, Field, reduxForm, FieldArray } from 'redux-form';
import { getPromiseState } from '../../../../utils/PromiseUtils';
import { YBLoading } from '../../../common/indicators';
import {
  isNonEmptyArray,
  isNonEmptyObject,
  isNonEmptyString,
  trimString
} from '../../../../utils/ObjectUtils';

import { FlexContainer, FlexGrow, FlexShrink } from '../../../common/flexbox/YBFlexBox';
import { NTPConfig, NTP_TYPES } from './NTPConfig';
import { ACCEPTABLE_CHARS } from '../../constants';

const validationIsRequired = (value) => (value && value.trim() !== '' ? undefined : 'Required');

class renderRegionInput extends Component {
  componentDidMount() {
    if (this.props.fields.length === 0) {
      this.props.fields.push({});
    }
  }
  render() {
    const { fields } = this.props;
    const regionMappingList = fields.map((item, idx) => (
      // eslint-disable-next-line react/no-array-index-key
      <FlexContainer key={idx}>
        <FlexGrow>
          <Row>
            <Col lg={6}>
              <Field
                name={`${item}.code`}
                validate={validationIsRequired}
                component={YBInputField}
                placeHolder="Region Name"
              />
            </Col>
            <Col lg={6}>
              <Field
                name={`${item}.subnet`}
                validate={validationIsRequired}
                component={YBInputField}
                placeHolder="Subnet ID"
              />
            </Col>
            <Col lg={12}>
              <Field
                name={`${item}.customImageId`}
                component={YBInputField}
                placeHolder="Custom Machine Image (Optional)"
                normalize={trimString}
              />
            </Col>
          </Row>
        </FlexGrow>
        <FlexShrink>
          <i
            className="fa fa-times fa-fw delete-row-btn"
            onClick={() => (fields.getAll().length > 1 ? fields.remove(idx) : null)}
          />
        </FlexShrink>
      </FlexContainer>
    ));
    return (
      <Fragment>
        <div className="divider"></div>
        <h5>Region mapping</h5>
        <div className="form-field-grid">
          {regionMappingList}
          <YBAddRowButton btnText="Add region" onClick={() => fields.push({})} />
        </div>
      </Fragment>
    );
  }
}

class GCPProviderInitView extends Component {
  constructor(props) {
    super(props);
    this.state = {
      gcpConfig: {},
      accountName: 'Google Cloud Provider',
      providerUUID: '',
      currentProvider: {},
      hostVpcVisible: true,
      networkSetupType: 'existing_vpc',
      credentialInputType: 'upload_service_account_json'
    };
    this.hostVpcToggled = this.hostVpcToggled.bind(this);
  }

  createProviderConfig = (vals) => {
    const self = this;
    const gcpCreateConfig = {};
    const perRegionMetadata = {};
    const ntpConfig = {
      setUpChrony: vals['setUpChrony'],
      showSetUpChrony: vals['setUpChrony'],
      ntpServers: vals['ntpServers']
    };
    if (isNonEmptyString(vals.destVpcId)) {
      gcpCreateConfig['network'] = vals.destVpcId;
      gcpCreateConfig['use_host_vpc'] = true;
    } else {
      gcpCreateConfig['use_host_vpc'] = false;
    }
    if (isNonEmptyString(vals.gcpProjectName)) {
      gcpCreateConfig['host_project_id'] = vals.gcpProjectName;
    }
    if (vals.network_setup !== 'new_vpc') {
      vals.regionMapping.forEach(
        (item) =>
          (perRegionMetadata[item.code] = {
            subnetId: item.subnet,
            customImageId: item.customImageId
          })
      );
    }
    if (isNonEmptyString(vals.firewall_tags)) {
      gcpCreateConfig['YB_FIREWALL_TAGS'] = vals.firewall_tags;
    }
    gcpCreateConfig['airGapInstall'] = vals.airGapInstall;
    gcpCreateConfig['sshPort'] = vals.sshPort;
    const providerName = vals.accountName;
    const configText = vals.gcpConfig;
    if (vals.credential_input === 'local_service_account') {
      gcpCreateConfig['use_host_credentials'] = true;
      return self.props.createGCPProvider(
        providerName,
        gcpCreateConfig,
        perRegionMetadata,
        ntpConfig
      );
    } else if (
      vals.credential_input === 'upload_service_account_json' &&
      isNonEmptyObject(configText)
    ) {
      gcpCreateConfig['use_host_credentials'] = false;
      const reader = new FileReader();
      reader.readAsText(configText);
      // Parse the file back to JSON, since the API controller endpoint doesn't support file upload
      reader.onloadend = function () {
        try {
          gcpCreateConfig['config_file_contents'] = JSON.parse(reader.result);
          return self.props.createGCPProvider(
            providerName,
            gcpCreateConfig,
            perRegionMetadata,
            ntpConfig
          );
        } catch (e) {
          toast.error('Invalid GCP config JSON file');
        }
        return null;
      };
    } else {
      // TODO: This scenario is not possible as one value in dropdown is selected by default
      // May we need to remove this
      toast.error('GCP Config JSON is required');
    }
  };

  isHostInGCP() {
    const { hostInfo } = this.props;
    return (
      isNonEmptyObject(hostInfo) &&
      isNonEmptyObject(hostInfo['gcp']) &&
      hostInfo['gcp']['error'] === undefined
    );
  }

  uploadGCPConfig(uploadFile) {
    this.setState({ gcpConfig: uploadFile[0] });
  }

  hostVpcToggled(event) {
    this.setState({ hostVpcVisible: !event.target.checked });
  }

  networkSetupChanged = (value) => {
    const { hostInfo } = this.props;
    if (value === 'host_vpc') {
      this.updateFormField('destVpcId', hostInfo['gcp']['network']);
      this.updateFormField('gcpProjectName', hostInfo['gcp']['host_project']);
    } else {
      this.updateFormField('destVpcId', null);
      this.updateFormField('gcpProjectName', null);
    }
    this.setState({ networkSetupType: value });
  };
  credentialInputChanged = (value) => {
    this.setState({ credentialInputType: value });
  };

  updateFormField = (field, value) => {
    this.props.dispatch(change('gcpProviderConfigForm', field, value));
  };

  render() {
    const { handleSubmit, configuredProviders, submitting, isBack, onBack } = this.props;
    if (getPromiseState(configuredProviders).isLoading()) {
      return <YBLoading />;
    }
    const network_setup_options = [
      <option key={1} value={'new_vpc'}>
        {'Create a new VPC (Beta)'}
      </option>,
      <option key={2} value={'existing_vpc'}>
        {'Specify an existing VPC'}
      </option>
    ];
    if (this.isHostInGCP()) {
      network_setup_options.push(
        <option key={3} value={'host_vpc'}>
          {'Use VPC of the Admin Console instance'}
        </option>
      );
    }
    const credential_input_options = [
      <option key={1} value={'upload_service_account_json'}>
        {'Upload Service Account config'}
      </option>,
      <option key={2} value={'local_service_account'}>
        {'Use Service Account on instance'}
      </option>
    ];
    let uploadConfigField = <span />;
    if (this.state.credentialInputType === 'upload_service_account_json') {
      let gcpConfigFileName = '';
      if (isNonEmptyObject(this.state.gcpConfig)) {
        gcpConfigFileName = this.state.gcpConfig.name;
      }
      uploadConfigField = (
        <Row className="config-provider-row">
          <Col lg={3}>
            <div className="form-item-custom-label">Provider Config</div>
          </Col>
          <Col lg={7}>
            <Field
              name="gcpConfig"
              component={YBDropZone}
              className="upload-file-button"
              title={'Upload GCP Config json file'}
            />
          </Col>
          <Col lg={4}>
            <div className="file-label">{gcpConfigFileName}</div>
          </Col>
        </Row>
      );
    }

    let destVpcField = <span />;
    let gcpProjectField = <span />;
    let regionInput = <span />;
    if (this.state.networkSetupType !== 'new_vpc') {
      destVpcField = (
        <Row className="config-provider-row">
          <Col lg={3}>
            <div className="form-item-custom-label">VPC Network Name</div>
          </Col>
          <Col lg={7}>
            <Field
              name="destVpcId"
              component={YBTextInputWithLabel}
              placeHolder="my-vpc-network-name"
              className={'gcp-provider-input-field'}
              isReadOnly={this.state.networkSetupType === 'host_vpc'}
            />
          </Col>
        </Row>
      );
      gcpProjectField = (
        <Row className="config-provider-row">
          <Col lg={3}>
            <div className="form-item-custom-label">Host Project Name</div>
          </Col>
          <Col lg={7}>
            <Field
              name="gcpProjectName"
              component={YBTextInputWithLabel}
              placeHolder="my-gcp-project-name"
              className={'gcp-provider-input-field'}
              isReadOnly={this.state.networkSetupType === 'host_vpc'}
            />
          </Col>
        </Row>
      );

      regionInput = <FieldArray name={'regionMapping'} component={renderRegionInput} />;
    }

    return (
      <div className="provider-config-container">
        <form name="gcpProviderConfigForm" onSubmit={handleSubmit(this.createProviderConfig)}>
          <div className="editor-container">
            <Row className="config-section-header">
              <Col lg={8}>
                <Row className="config-provider-row">
                  <Col lg={3}>
                    <div className="form-item-custom-label">Name</div>
                  </Col>
                  <Col lg={7}>
                    <Field
                      name="accountName"
                      placeHolder="Google Cloud Platform"
                      component={YBTextInputWithLabel}
                      className={'gcp-provider-input-field'}
                    />
                  </Col>
                </Row>
                <Row>
                  <Col lg={3}>
                    <div className="form-item-custom-label">Credential Type</div>
                  </Col>
                  <Col lg={7}>
                    <Field
                      name="credential_input"
                      component={YBSelectWithLabel}
                      options={credential_input_options}
                      onInputChanged={this.credentialInputChanged}
                    />
                  </Col>
                </Row>
                {uploadConfigField}
                <Row>
                  <Col lg={3}>
                    <div className="form-item-custom-label">Air Gap Installation</div>
                  </Col>
                  <Col lg={7}>
                    <Field name="airGapInstall" component={YBToggle} />
                  </Col>
                </Row>
                <Row>
                  <Col lg={3}>
                    <div className="form-item-custom-label">SSH Port</div>
                  </Col>
                  <Col lg={7}>
                    <Field name="sshPort" component={YBNumericInput} />
                  </Col>
                </Row>
                <Row>
                  <Col lg={3}>
                    <div className="form-item-custom-label">VPC Setup</div>
                  </Col>
                  <Col lg={7}>
                    <Field
                      name="network_setup"
                      component={YBSelectWithLabel}
                      options={network_setup_options}
                      onInputChanged={this.networkSetupChanged}
                    />
                  </Col>
                </Row>
                <Row>
                  <Col lg={3}>
                    <div className="form-item-custom-label">Firewall Tags</div>
                  </Col>
                  <Col lg={7}>
                    <Field
                      name="firewall_tags"
                      placeHolder="my-firewall-tag-1,my-firewall-tag-2"
                      component={YBTextInputWithLabel}
                      className={'gcp-provider-input-field'}
                    />
                  </Col>
                </Row>
                {gcpProjectField}
                {destVpcField}
                {regionInput}
                <Row>
                  <Col lg={3}>
                    <div className="form-item-custom-label">NTP Setup</div>
                  </Col>
                  <Col lg={7}>
                    <NTPConfig onChange={this.updateFormField} hideHelp={true} />
                  </Col>
                </Row>
              </Col>
            </Row>
          </div>
          <div className="form-action-button-container">
            <YBButton
              btnText={'Save'}
              disabled={submitting}
              btnClass={'btn btn-default save-btn'}
              btnType="submit"
            />
            {isBack && (
              <YBButton
                onClick={onBack}
                btnText="Back"
                btnClass="btn btn-default"
                disabled={submitting}
              />
            )}
          </div>
        </form>
      </div>
    );
  }
}

const validate = (values) => {
  const errors = { regionMapping: [] };
  if (!isNonEmptyString(values.accountName)) {
    errors.accountName = 'Account Name is Required';
  } else if (!ACCEPTABLE_CHARS.test(values.accountName)) {
    errors.accountName = 'Account Name cannot have special characters except - and _';
  }

  if (!isNonEmptyObject(values.gcpConfig)) {
    errors.gcpConfig = 'Provider Config is Required';
  }
  if (values.network_setup === 'existing_vpc') {
    if (!isNonEmptyString(values.gcpProjectName)) {
      errors.gcpProjectName = 'Project Name is Required';
    }
    if (!isNonEmptyString(values.destVpcId)) {
      errors.destVpcId = 'VPC Network Name is Required';
    }
  }
  if (values.ntp_option === NTP_TYPES.MANUAL && values.ntpServers.length === 0) {
    errors.ntpServers = 'NTP servers cannot be empty';
  }

  if (values.regionMapping && isNonEmptyArray(values.regionMapping)) {
    const requestedRegions = new Set();
    values.regionMapping.forEach((region, idx) => {
      if (requestedRegions.has(region.code)) {
        errors.regionMapping[idx] = { code: 'Duplicate region code is not allowed.' };
      } else {
        requestedRegions.add(region.code);
      }
    });
  }
  return errors;
};

function mapStateToProps(state) {
  return {
    initialValues: {
      accountName: '',
      credential_input: 'upload_service_account_json',
      airGapInstall: false,
      network_setup: 'existing_vpc',
      ntp_option: NTP_TYPES.PROVIDER,
      ntpServers: [],
      setUpChrony: true
    }
  };
}

export default connect(
  mapStateToProps,
  null
)(
  reduxForm({
    form: 'gcpProviderConfigForm',
    validate,
    touchOnChange: true
  })(GCPProviderInitView)
);

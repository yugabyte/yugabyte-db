// Copyright (c) YugaByte, Inc.

import React, {Component} from 'react';
import { Row, Col } from 'react-bootstrap';
import { YBButton } from '../../../common/forms/fields';
import { YBTextInputWithLabel, YBSelectWithLabel, YBDropZone } from '../../../common/forms/fields';
import { change, Field } from 'redux-form';
import { getPromiseState } from 'utils/PromiseUtils';
import { YBLoading } from '../../../common/indicators';
import { isNonEmptyObject, isNonEmptyString } from 'utils/ObjectUtils';
import { reduxForm } from 'redux-form';

class GCPProviderInitView extends Component {
  constructor(props) {
    super(props);
    this.state = {
      gcpConfig: {},
      accountName: "Google Cloud Provider",
      providerUUID: "",
      currentProvider: {},
      hostVpcVisible: true,
      networkSetupType: "new_vpc"
    };
    this.hostVpcToggled = this.hostVpcToggled.bind(this);
  }

  createProviderConfig = vals => {
    const self = this;
    const configText = vals.gcpConfig;
    if (isNonEmptyObject(configText)) {
      const providerName = vals.accountName;
      const reader = new FileReader();
      reader.readAsText(configText);
      // Parse the file back to JSON, since the API controller endpoint doesn't support file upload
      reader.onloadend = function () {
        try {
          const gcpCreateConfig = {
            "config_file_contents": JSON.parse(reader.result)
          };
          if (isNonEmptyString(vals.destVpcId)) {
            gcpCreateConfig["network"] = vals.destVpcId;
            gcpCreateConfig["use_host_vpc"] = true;
          } else {
            gcpCreateConfig["use_host_vpc"] = false;
          }
          self.props.createGCPProvider(providerName, gcpCreateConfig);
        } catch (e) {
          self.setState({"error": "Invalid GCP config JSON file"});
        }
      };
    } else {
      this.setState({"error": "GCP Config JSON is required"});
    }
  };

  isHostInGCP() {
    const { hostInfo } = this.props;
    // Removed the !IN_DEVELOPMENT_MODE check because GCP bootstrap should not break anything.
    return isNonEmptyObject(hostInfo) && isNonEmptyObject(hostInfo["gcp"]) &&
      hostInfo["gcp"]["error"] === undefined;
  }

  uploadGCPConfig(uploadFile) {
    this.setState({gcpConfig: uploadFile[0]});
  }

  hostVpcToggled(event) {
    this.setState({hostVpcVisible: !event.target.checked});
  }

  networkSetupChanged = (value) => {
    const { hostInfo } = this.props;
    if (value === "host_vpc") {
      this.updateFormField("destVpcId", hostInfo["gcp"]["network"]);
    } else {
      this.updateFormField("destVpcId", null);
    }
    this.setState({networkSetupType: value});
  }

  updateFormField = (field, value) => {
    this.props.dispatch(change("gcpProviderConfigForm", field, value));
  };

  render() {
    const { handleSubmit, configuredProviders, submitting } = this.props;
    if (getPromiseState(configuredProviders).isLoading()) {
      return <YBLoading />;
    }
    let gcpConfigFileName = "";
    if (isNonEmptyObject(this.state.gcpConfig)) {
      gcpConfigFileName = this.state.gcpConfig.name;
    }

    const network_setup_options = [
      <option key={1} value={"new_vpc"}>{"Create a new VPC"}</option>,
      <option key={2} value={"existing_vpc"}>{"Specify an existing VPC"}</option>
    ];
    if (this.isHostInGCP()) {
      network_setup_options.push(
        <option key={3} value={"host_vpc"}>{"Use VPC of the Admin Console instance"}</option>
      );
    }

    let destVpcField = <span />;
    if (this.state.networkSetupType !== "new_vpc") {
      destVpcField = (
        <div>
          <Col lg={3}>
            <div className="form-item-custom-label">
              VPC Network Name
            </div>
          </Col>
          <Col lg={7}>
            <Field name="destVpcId" component={YBTextInputWithLabel}
                placeHolder="my-vpc-network-name"
                className={"gcp-provider-input-field"}
                isReadOnly={this.state.networkSetupType === "host_vpc"}/>
          </Col>
        </div>
      );
    }

    return (
      <div className="provider-config-container">
        <form name="gcpProviderConfigForm" onSubmit={handleSubmit(this.createProviderConfig)}>
          <div className="editor-container">
            <Row>
              <Col lg={8}>
                <Row className="config-provider-row">
                  <Col lg={3}>
                    <div className="form-item-custom-label">Name</div>
                  </Col>
                  <Col lg={7}>
                    <Field name="accountName" placeHolder="Google Cloud Platform"
                           component={YBTextInputWithLabel} className={"gcp-provider-input-field"}/>
                  </Col>
                </Row>
                <Row className="config-provider-row">
                  <Col lg={3}>
                    <div className="form-item-custom-label">Provider Config</div>
                  </Col>
                  <Col lg={7}>
                    <Field name="gcpConfig" component={YBDropZone} className="upload-file-button" title={"Upload GCP Config json file"}/>
                  </Col>
                  <Col lg={4}>
                    <div className="file-label">{gcpConfigFileName}</div>
                  </Col>
                </Row>
                <Row>
                  <Col lg={3}>
                    <div className="form-item-custom-label">VPC Setup</div>
                  </Col>
                  <Col lg={7}>
                    <Field name="network_setup" component={YBSelectWithLabel}
                      options={network_setup_options}
                      onInputChanged={this.networkSetupChanged} />
                  </Col>
                </Row>
                <Row className="config-provider-row">
                  {destVpcField}
                </Row>
              </Col>
            </Row>
          </div>
          <div className="form-action-button-container">
            <YBButton btnText={"Save"} btnDisabled={submitting} btnClass={"btn btn-default save-btn"} btnType="submit"/>
          </div>
        </form>
      </div>
    );
  }
}

const validate = (values) => {
  const errors = {};
  if (!isNonEmptyString(values.accountName)) {
    errors.accountName = 'Account Name is Required';
  }
  if (!isNonEmptyObject(values.gcpConfig)) {
    errors.gcpConfig = 'Provider Config is Required';
  }
  if (!isNonEmptyString(values.destVpcId) &&
      values.network_setup === "existing_vpc") {
    errors.destVpcId = 'VPC Network name is Required';
  }
  return errors;
};

export default reduxForm({
  form: 'gcpProviderConfigForm',
  validate
})(GCPProviderInitView);

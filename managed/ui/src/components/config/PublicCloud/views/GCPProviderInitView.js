// Copyright (c) YugaByte, Inc.

import React, {Component} from 'react';
import { Row, Col } from 'react-bootstrap';
import { YBButton, YBToggle } from '../../../common/forms/fields';
import { YBTextInput } from '../../../common/forms/fields';
import { Field } from 'redux-form';
import { getPromiseState } from 'utils/PromiseUtils';
import { YBLoading } from '../../../common/indicators';
import { isNonEmptyObject, isNonEmptyString } from 'utils/ObjectUtils';
import Dropzone from 'react-dropzone';
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
    };
    this.hostVpcToggled = this.hostVpcToggled.bind(this);
  }

  createProviderConfig = vals => {
    const self = this;
    const configText = this.state.gcpConfig;
    const {hostInfo} = this.props;
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
          const useHostVpc = Boolean(vals.useHostVpc);
          if (self.isHostInGCP() && useHostVpc) {
            gcpCreateConfig["network"] = hostInfo["gcp"]["network"];
            gcpCreateConfig["use_host_vpc"] = useHostVpc;
          } else if (isNonEmptyString(vals.destVpcId)) {
            gcpCreateConfig["network"] = vals.destVpcId;
            gcpCreateConfig["use_host_vpc"] = true;
          } else {
            // This already implies useHostVpc === false, as it defaults to false outside of GCP.
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

  render() {
    const { handleSubmit, configuredProviders} = this.props;
    if (getPromiseState(configuredProviders).isLoading()) {
      return <YBLoading />;
    }
    let gcpConfigFileName = "";
    if (isNonEmptyObject(this.state.gcpConfig)) {
      gcpConfigFileName = this.state.gcpConfig.name;
    }
    const subLabel = "Disabled if host is not on GCP";
    let destVpcField = <span />;
    if (this.state.hostVpcVisible) {
      destVpcField = (
        <div>
          <Col lg={2}>
            <div className="form-item-custom-label">
              Custom VPC Network
            </div>
          </Col>
          <Col lg={10}>
            <Field name="destVpcId" component={YBTextInput} placeHolder="VPC Network Name"
                   className={"gcp-provider-input-field"} />
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
                  <Col lg={2}>
                    <div className="form-item-custom-label">Name</div>
                  </Col>
                  <Col lg={10}>
                    <Field name="accountName" placeHolder="Google Cloud Platform"
                           component={YBTextInput} className={"gcp-provider-input-field"}/>
                  </Col>
                </Row>
                <Row className="config-provider-row">
                  <Col lg={2}>
                    <div className="form-item-custom-label">Provider Config</div>
                  </Col>
                  <Col lg={6}>
                    <Dropzone onDrop={this.uploadGCPConfig.bind(this)} className="upload-file-button">
                      <p>Upload GCP Config json file</p>
                    </Dropzone>
                  </Col>
                  <Col lg={4}>
                    <div className="file-label">{gcpConfigFileName}</div>
                  </Col>
                </Row>
                <Row className="config-provider-row">
                  {destVpcField}
                </Row>
                <Row className="config-provider-row">
                  <Col lg={2}>
                    <Field name="useHostVpc"
                           component={YBToggle}
                           label="Use Host's VPC"
                           subLabel={subLabel}
                           defaultChecked={!this.state.hostVpcVisible}
                           isReadOnly={!this.isHostInGCP()}
                           onToggle={this.hostVpcToggled} />
                  </Col>
                </Row>
              </Col>
            </Row>
          </div>
          <div className="form-action-button-container">
            <YBButton btnText={"Save"} btnClass={"btn btn-default save-btn"} btnType="submit"/>
          </div>
        </form>
      </div>
    );
  }
}

export default reduxForm({
  form: 'gcpProviderConfigForm'
})(GCPProviderInitView);

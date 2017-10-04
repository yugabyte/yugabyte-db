// Copyright (c) YugaByte, Inc.

import React, {Component} from 'react';
import { Row, Col } from 'react-bootstrap';
import { YBButton } from '../../../common/forms/fields';
import { YBTextInput } from '../../../common/forms/fields';
import {Field} from 'redux-form';
import {getPromiseState} from 'utils/PromiseUtils';
import {YBLoadingIcon} from '../../../common/indicators';
import {isNonEmptyObject, } from 'utils/ObjectUtils';
import Dropzone from 'react-dropzone';
import { reduxForm } from 'redux-form';

class GCPProviderInitView extends Component {
  constructor(props) {
    super(props);
    this.createProviderConfig = this.createProviderConfig.bind(this);
    this.state = {
      gcpConfig: {},
      accountName: "Google Cloud Provider",
      providerUUID: "",
      currentProvider: {},
    };
  }

  createProviderConfig(vals) {
    const self = this;
    const configText = this.state.gcpConfig;
    if(isNonEmptyObject(configText)) {
      const providerName = vals.accountName;
      const reader = new FileReader();
      reader.readAsText(configText);
      // Parse the file back to JSON, since the API controller endpoint doesn't support file upload
      reader.onloadend = function () {
        let gcpConfig = {};
        try {
          gcpConfig = JSON.parse(reader.result);
          self.props.createGCPProvider(providerName, gcpConfig);
        } catch (e) {
          self.setState({"error": "Invalid GCP config JSON file"});
        }
      };
    } else {
      this.setState({"error": "GCP Config JSON is required"});
    }
  }

  uploadGCPConfig(uploadFile) {
    this.setState({gcpConfig: uploadFile[0]});
  }

  render() {
    const { handleSubmit, configuredProviders} = this.props;
    if (getPromiseState(configuredProviders).isLoading()) {
      return <YBLoadingIcon/>;
    }
    let gcpConfigFileName = "";
    if (isNonEmptyObject(this.state.gcpConfig)) {
      gcpConfigFileName = this.state.gcpConfig.name;
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

// Copyright (c) YugaByte, Inc.

import React, {Component} from 'react';
import { Row, Col } from 'react-bootstrap';
import { YBButton } from '../../../common/forms/fields';
import { YBTextInputWithLabel, YBSelect, YBDropZone, YBTextArea } from '../../../common/forms/fields';
import { Field } from 'redux-form';
import { isNonEmptyObject, isDefinedNotNull } from 'utils/ObjectUtils';
import { REGION_METADATA, KUBERNETES_PROVIDERS } from 'config';
import { withRouter } from 'react-router';
import { YBPanelItem } from '../../../panels';

class CreateKubernetesConfiguration extends Component {
  constructor(props) {
    super(props);
    this.state = {
      kubeConfig: {},
      accountName: "Kube Config",
      providerUUID: "",
      currentProvider: {}
    };
  }

  readUploadedFileAsText = (inputFile, isRequired) => {
    const fileReader = new FileReader();
    return new Promise((resolve, reject) => {
      fileReader.onloadend = () => {
        resolve(fileReader.result);
      };
      // Parse the file back to JSON, since the API controller endpoint doesn't support file upload
      if (isDefinedNotNull(inputFile)) {
        fileReader.readAsText(inputFile);
      }
      if (!isRequired && !isDefinedNotNull(inputFile)) {
        resolve(null);
      }
    });
  };

  createProviderConfig = vals => {
    const self = this;
    const kubeConfigFile = vals.kubeConfig;
    const pullSecretFile = vals.pullSecret;
    const providerName = vals.accountName;
    const readerConfig = this.readUploadedFileAsText(kubeConfigFile, true);
    const readerSecret = this.readUploadedFileAsText(pullSecretFile, false);

    // Catch all onload events for configs
    Promise.all([readerConfig, readerSecret]).then(configs => {
      const providerConfig = {
        "KUBECONFIG_CONTENT": configs[0],
        "KUBECONFIG_NAME": kubeConfigFile.name,
        "KUBECONFIG_PROVIDER": self.props.type,
        "KUBECONFIG_SERVICE_ACCOUNT": vals.serviceAccount,
        "KUBECONFIG_ANNOTATIONS": vals.annotations,
        "KUBECONFIG_STORAGE_CLASSES": vals.storageClasses,
        "KUBECONFIG_IMAGE_REGISTRY": vals.imageRegistry,
        "KUBECONFIG_IMAGE_PULL_SECRET_NAME": vals.imagePullSecretName,
        "KUBECONFIG_PULL_SECRET_NAME": pullSecretFile && pullSecretFile.name,
        "KUBECONFIG_PULL_SECRET_CONTENT": configs[1]
      };
      const regionData = REGION_METADATA.find((region) => region.code === vals.regionCode);
      const zoneData = [vals.zoneLabel.replace(" ", "-")];

      Object.keys(providerConfig).forEach((key) => { if (typeof providerConfig[key] === 'string' || providerConfig[key] instanceof String) providerConfig[key] = providerConfig[key].trim(); });
      Object.keys(regionData).forEach((key) =>     { if (typeof regionData[key] === 'string' ||     regionData[key] instanceof String)     regionData[key] =     regionData[key].trim(); });
      Object.keys(zoneData).forEach((key) =>       { if (typeof zoneData[key] === 'string' ||       providerConfig[key] instanceof String) zoneData[key] =       zoneData[key].trim(); });
      self.props.createKubernetesProvider(providerName.trim(), providerConfig, regionData, zoneData);
    }, reason => {
      console.warn("File Upload gone wrong. "+reason);
    });
    this.props.onSubmit(true);
  }

  uploadConfig = (uploadFile) => {
    this.setState({kubeConfig: uploadFile});
  }

  uploadPullSecret = (uploadFile) => {
    this.setState({pullSecret: uploadFile});
  }

  render() {
    const { handleSubmit, submitting, type } = this.props;
    let kubeConfigFileName = "";
    if (isNonEmptyObject(this.state.kubeConfig)) {
      kubeConfigFileName = this.state.kubeConfig.name;
    }
    let pullSecretFileName = "";
    if (isNonEmptyObject(this.state.pullSecret)) {
      pullSecretFileName = this.state.pullSecret.name;
    }
    const regionOptions = REGION_METADATA.map((region) => {
      return (<option value={region.code} key={region.code}>{region.name}</option>);
    });
    regionOptions.unshift(<option value="" key={0}>Select</option>);
    const providerTypeMetadata = KUBERNETES_PROVIDERS.find((providerType) => providerType.code === type);

    return (
      <YBPanelItem
        header={
          <h2 className="table-container-title">Create { providerTypeMetadata.name }</h2>
        }
        body={
          <div className="provider-config-container">
            <form name="kubernetesConfigForm"
                  onSubmit={handleSubmit(this.createProviderConfig)}>
              <div className="editor-container">
                <Row>
                  <Col lg={8}>
                    <Row className="config-provider-row">
                      <Col lg={3}>
                        <div className="form-item-custom-label">Name</div>
                      </Col>
                      <Col lg={7}>
                        <Field name="accountName" placeHolder="Kube Config name"
                               component={YBTextInputWithLabel} insetError={true}
                               className={"kube-provider-input-field"}/>
                      </Col>
                    </Row>
                    <Row className="config-provider-row">
                      <Col lg={3}>
                        <div className="form-item-custom-label">Kube Config</div>
                      </Col>
                      <Col lg={7}>
                        <Field name="kubeConfig" component={YBDropZone}
                          className="upload-file-button"
                          onChange={this.uploadConfig}
                          title={"Upload Kube Config file"}/>
                      </Col>
                      <Col lg={4}>
                        <div className="file-label">{kubeConfigFileName}</div>
                      </Col>
                    </Row>
                    <Row className="config-provider-row">
                      <Col lg={3}>
                        <div className="form-item-custom-label">Service Account</div>
                      </Col>
                      <Col lg={7}>
                        <Field name="serviceAccount" placeHolder="Service Account name"
                               component={YBTextInputWithLabel}
                               insetError={true}
                               className={"kube-provider-input-field"}/>
                      </Col>
                    </Row>

                    <Row className="config-provider-row">
                      <Col lg={3}>
                        <div className="form-item-custom-label">Region</div>
                      </Col>
                      <Col lg={7}>
                        <Field name={"regionCode"} component={YBSelect}
                               insetError={true} options={regionOptions} />
                      </Col>
                    </Row>
                    <Row className="config-provider-row">
                      <Col lg={3}>
                        <div className="form-item-custom-label">Zone</div>
                      </Col>
                      <Col lg={7}>
                        <Field name="zoneLabel" placeHolder="Zone Label"
                               component={YBTextInputWithLabel}
                               insetError={true}
                               className={"kube-provider-input-field"}/>
                      </Col>
                    </Row>

                    <Row className="config-provider-row">
                      <Col lg={3}>
                        <div className="form-item-custom-label">Image Registry</div>
                      </Col>
                      <Col lg={7}>
                        <Field name="imageRegistry" placeHolder="Optional Image Registry"
                               component={YBTextInputWithLabel}
                               insetError={true}
                               className={"kube-provider-input-field"}/>
                      </Col>
                    </Row>
                    <Row className="config-provider-row">
                      <Col lg={3}>
                        <div className="form-item-custom-label">Pull Secret File</div>
                      </Col>
                      <Col lg={7}>
                        <Field name="pullSecret" component={YBDropZone}
                          className="upload-file-button"
                          onChange={this.uploadPullSecret}
                          title={"Upload Pull Secret file"}/>
                      </Col>
                      <Col lg={4}>
                        <div className="file-label">{pullSecretFileName}</div>
                      </Col>
                    </Row>
                    <Row className="config-provider-row">
                      <Col lg={3}>
                        <div className="form-item-custom-label">Image Pull Secret Name</div>
                      </Col>
                      <Col lg={7}>
                        <Field name="imagePullSecretName" placeHolder="Optional Image Pull Secret Name"
                               component={YBTextInputWithLabel}
                               insetError={true}
                               className={"kube-provider-input-field"}/>
                      </Col>
                    </Row>
                    
                    <Row className="config-provider-row">
                      <Col lg={3}>
                        <div className="form-item-custom-label">Storage Classes</div>
                      </Col>
                      <Col lg={7}>
                        <Field name="storageClasses" placeHolder="Storage Class Names (default Standard)"
                               component={YBTextInputWithLabel}
                               insetError={true}
                               className={"kube-provider-input-field"}/>
                      </Col>
                    </Row>
                    <Row className="config-provider-row">
                      <Col lg={3}>
                        <div className="form-item-custom-label">Annotations</div>
                      </Col>
                      <Col lg={7}>
                        <Field name="annotations" placeHolder="Optional Annotation for Internal Load Balancer"
                               component={YBTextArea}
                               insetError={true}
                               className={"kube-provider-input-field"}/>
                      </Col>
                    </Row>

                  </Col>
                </Row>
              </div>
              <div className="form-action-button-container">
                <YBButton btnText={"Save"} btnDisabled={submitting}
                          btnClass={"btn btn-default save-btn"}
                          btnType="submit" />
                <YBButton btnText={"Cancel"} btnDisabled={submitting}
                          btnClass={"btn btn-default cancel-btn"}
                          btnType="button" onClick={this.props.onCancel} />
              </div>
            </form>
          </div>
        }
      />
    );
  }
}

export default withRouter(CreateKubernetesConfiguration);

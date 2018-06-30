// Copyright (c) YugaByte, Inc.

import React, {Component} from 'react';
import { Row, Col } from 'react-bootstrap';
import { YBButton } from '../../../common/forms/fields';
import { YBTextInputWithLabel, YBSelect, YBDropZone } from '../../../common/forms/fields';
import { Field } from 'redux-form';
import { isNonEmptyObject } from 'utils/ObjectUtils';
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

  createProviderConfig = vals => {
    const self = this;
    const kubeConfigFile = vals.kubeConfig;
    const providerName = vals.accountName;
    const reader = new FileReader();
    reader.readAsText(kubeConfigFile);
    // Parse the file back to JSON, since the API controller endpoint doesn't support file upload
    reader.onloadend = function () {
      const providerConfig = {
        "KUBECONFIG_CONTENT": reader.result,
        "KUBECONFIG_NAME": kubeConfigFile.name,
        "KUBECONFIG_PROVIDER": self.props.type,
        "KUBECONFIG_SERVICE_ACCOUNT": vals.serviceAccount
      };
      const regionData = REGION_METADATA.find((region) => region.code === vals.regionCode);
      const zoneData = [vals.zoneLabel.replace(" ", "-")];
      const instanceTypeData = {
        "instanceTypeCode": "Large",
        "numCores": 2,
        "memSizeGB": 10,
        "volumeDetailsList": [{
          "volumeSizeGB": "100",
          "volumeType": "SSD"
        }]
      };
      self.props.createKubernetesProvider(providerName, providerConfig, regionData, zoneData, instanceTypeData);
    };
    this.props.onSubmit();
  }

  uploadConfig = (uploadFile) => {
    this.setState({kubeConfig: uploadFile});
  }

  render() {
    const { handleSubmit, submitting, type } = this.props;
    let kubeConfigFileName = "";
    if (isNonEmptyObject(this.state.kubeConfig)) {
      kubeConfigFileName = this.state.kubeConfig.name;
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

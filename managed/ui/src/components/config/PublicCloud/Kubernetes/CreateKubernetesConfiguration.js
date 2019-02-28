// Copyright (c) YugaByte, Inc.

import React, {Component} from 'react';
import { Row, Col } from 'react-bootstrap';
import { YBButton } from '../../../common/forms/fields';
import { YBFormSelect, YBFormInput, YBFormDropZone } from '../../../common/forms/fields';
import { isNonEmptyObject, isDefinedNotNull, isNonEmptyString } from 'utils/ObjectUtils';
import { REGION_METADATA, KUBERNETES_PROVIDERS } from 'config';
import { withRouter } from 'react-router';
import { Formik, Field } from 'formik';
import * as Yup from "yup";
import JsYaml from "js-yaml";


class CreateKubernetesConfiguration extends Component {
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

  createProviderConfig = (vals, setSubmitting) => {
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
        "KUBECONFIG_PROVIDER": vals.providerType,
        "KUBECONFIG_SERVICE_ACCOUNT": vals.serviceAccount,
        "KUBECONFIG_STORAGE_CLASSES": vals.storageClasses,
        "KUBECONFIG_IMAGE_REGISTRY": vals.imageRegistry
      };
      // TODO: fetch the service account name from the kubeconfig.

      if (isNonEmptyObject(pullSecretFile)) {
        const pullSecretYaml = JsYaml.safeLoad(configs[1]);
        Object.assign(providerConfig, {
          "KUBECONFIG_IMAGE_PULL_SECRET_NAME": pullSecretYaml.metadata.name,
          "KUBECONFIG_PULL_SECRET_NAME": pullSecretFile.name,
          "KUBECONFIG_PULL_SECRET_CONTENT": configs[1]
        });
      }

      if (isNonEmptyString(vals.annotations)) {
        Object.assign(providerConfig, {
          "KUBECONFIG_ANNOTATIONS": vals.annotations
        });
      }
      const regionData = REGION_METADATA.find((region) => region.code === vals.regionCode);
      const zoneData = [vals.zoneLabel.replace(" ", "-")];

      Object.keys(providerConfig).forEach((key) => { if (typeof providerConfig[key] === 'string' || providerConfig[key] instanceof String) providerConfig[key] = providerConfig[key].trim(); });
      Object.keys(regionData).forEach((key) =>     { if (typeof regionData[key] === 'string' ||     regionData[key] instanceof String)     regionData[key] =     regionData[key].trim(); });
      Object.keys(zoneData).forEach((key) =>       { if (typeof zoneData[key] === 'string' ||       providerConfig[key] instanceof String) zoneData[key] =       zoneData[key].trim(); });
      self.props.createKubernetesProvider(providerName.trim(), providerConfig, regionData, zoneData);
    }, reason => {
      console.warn("File Upload gone wrong. "+reason);
    });
    this.props.toggleListView(true);
    setSubmitting(false);
  }

  render() {
    const { type } = this.props;
    const regionOptions = REGION_METADATA.map((region) => {
      return {value: region.code, label: region.name};
    });

    const providerTypeMetadata = KUBERNETES_PROVIDERS.find(
      (providerType) => providerType.code === type
    );
    let title = "Create Managed Kubernetes config";
    let providerTypeOptions = null;
    if (providerTypeMetadata) {
      providerTypeOptions = [
        {value: providerTypeMetadata.code, label: providerTypeMetadata.name }
      ];
      title = "Create " + providerTypeMetadata.name;
    } else {
      providerTypeOptions = KUBERNETES_PROVIDERS.map((provider) => {
        return {value: provider.code, label: provider.name};
      }).filter((p) => p.value !== "pks");
    }

    const initialValues = {
      providerType: (providerTypeMetadata ? providerTypeMetadata.code : "gke"),
      accountName: "",
      serviceAccount: "",
      regionCode: "",
      zoneLabel: "",
      imageRegistry: "",
      storageClasses: "",
      annotations: ""
    };

    const validationSchema = Yup.object().shape({
      accountName: Yup.string()
      .required('Config name is Required'),

      kubeConfig: Yup.string()
      .required('Kube Config is Required'),

      serviceAccount: Yup.string()
      .required('Service Account name is Required'),

      regionCode: Yup.string()
      .required('Region name is Required'),

      zoneLabel: Yup.string()
      .required('Zone name is Required'),

    });

    return (
      <div>
        <h2 className="table-container-title">{title}</h2>
        <div className="provider-config-container">
          <Formik
            validationSchema={validationSchema}
            initialValues={initialValues}
            onSubmit={(values, { setSubmitting }) => {
              const payload = {
                ...values,
                providerType: values.providerType.value,
                regionCode: values.regionCode.value,
              };
              this.createProviderConfig(payload, setSubmitting);
            }}
            render={props => (
              <form name="kubernetesConfigForm"
                    onSubmit={props.handleSubmit}>
                <div className="editor-container">
                  <Row>
                    <Col lg={8}>
                      <Row className="config-provider-row">
                        <Col lg={3}>
                          <div className="form-item-custom-label">Type</div>
                        </Col>
                        <Col lg={7}>
                          <Field name={"providerType"} component={YBFormSelect}
                                 insetError={true} options={providerTypeOptions} />
                        </Col>
                      </Row>
                      <Row className="config-provider-row">
                        <Col lg={3}>
                          <div className="form-item-custom-label">Name</div>
                        </Col>
                        <Col lg={7}>
                          <Field name="accountName" placeholder="Kube Config name"
                                 component={YBFormInput} insetError={true}
                                 className={"kube-provider-input-field"}/>
                        </Col>
                      </Row>
                      <Row className="config-provider-row">
                        <Col lg={3}>
                          <div className="form-item-custom-label">Kube Config</div>
                        </Col>
                        <Col lg={7}>
                          <Field name="kubeConfig" component={YBFormDropZone}
                            className="upload-file-button"
                            title={"Upload Kube Config file"}/>
                        </Col>
                      </Row>
                      <Row className="config-provider-row">
                        <Col lg={3}>
                          <div className="form-item-custom-label">Service Account</div>
                        </Col>
                        <Col lg={7}>
                          <Field name="serviceAccount" placeholder="Service Account name"
                                 component={YBFormInput}
                                 insetError={true}
                                 className={"kube-provider-input-field"}/>
                        </Col>
                      </Row>

                      <Row className="config-provider-row">
                        <Col lg={3}>
                          <div className="form-item-custom-label">Region</div>
                        </Col>
                        <Col lg={7}>
                          <Field name={"regionCode"} component={YBFormSelect}
                                 insetError={true} options={regionOptions} />
                        </Col>
                      </Row>
                      <Row className="config-provider-row">
                        <Col lg={3}>
                          <div className="form-item-custom-label">Zone</div>
                        </Col>
                        <Col lg={7}>
                          <Field name="zoneLabel" placeholder="Zone Label"
                                 component={YBFormInput}
                                 insetError={true}
                                 className={"kube-provider-input-field"}/>
                        </Col>
                      </Row>

                      <Row className="config-provider-row">
                        <Col lg={3}>
                          <div className="form-item-custom-label">Image Registry</div>
                        </Col>
                        <Col lg={7}>
                          <Field name="imageRegistry" placeholder="Optional Image Registry"
                                 component={YBFormInput}
                                 insetError={true}
                                 className={"kube-provider-input-field"}/>
                        </Col>
                      </Row>
                      <Row className="config-provider-row">
                        <Col lg={3}>
                          <div className="form-item-custom-label">Pull Secret File</div>
                        </Col>
                        <Col lg={7}>
                          <Field name="pullSecret" component={YBFormDropZone}
                            className="upload-file-button"
                            title={"Upload Pull Secret file"}/>
                        </Col>
                      </Row>
                      <Row className="config-provider-row">
                        <Col lg={3}>
                          <div className="form-item-custom-label">Storage Classes</div>
                        </Col>
                        <Col lg={7}>
                          <Field name="storageClasses" placeholder="Storage Class Names (default Standard)"
                                 component={YBFormInput}
                                 insetError={true}
                                 className={"kube-provider-input-field"}/>
                        </Col>
                      </Row>
                      <Row className="config-provider-row">
                        <Col lg={3}>
                          <div className="form-item-custom-label">Annotations</div>
                        </Col>
                        <Col lg={7}>
                          <Field name="annotations" placeholder="Optional Annotation for Internal Load Balancer"
                                 component={YBFormInput}
                                 componentClass="textarea"
                                 insetError={true}
                                 className={"kube-provider-input-field"}/>
                        </Col>
                      </Row>

                    </Col>
                  </Row>
                </div>
                <div className="form-action-button-container">
                  <YBButton btnText={"Save"} btnDisabled={props.isSubmitting}
                            btnClass={"btn btn-default save-btn"}
                            btnType="submit" />
                  {this.props.hasConfigs && <YBButton btnText={"Cancel"} btnDisabled={props.isSubmitting}
                            btnClass={"btn btn-default cancel-btn"}
                            btnType="button" onClick={this.props.toggleListView} />}
                </div>
              </form>
            )}
          />
        </div>
      </div>
    );
  }
}

export default withRouter(CreateKubernetesConfiguration);

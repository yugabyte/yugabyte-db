// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { Field, Formik } from 'formik';
import * as Yup from "yup";
import 'react-bootstrap-multiselect/css/bootstrap-multiselect.css';
import { YBModal, YBFormToggle, YBFormSelect, YBFormDropZone } from '../../common/forms/fields';
import { readUploadedFile } from "../../../utils/UniverseUtils";
import { isNonEmptyObject } from 'utils/ObjectUtils';

export default class EncryptionKeyModal extends Component {
  componentDidMount() {
    const { configList, fetchKMSConfigList } = this.props;
    if (!configList.data.length) {
      fetchKMSConfigList();
    }
  }

  handleSubmitForm = (values) => {
    const { currentUniverse: { data: { universeUUID, universeDetails }},
            setEncryptionKey, handleSubmitKey } = this.props;
    const primaryCluster = universeDetails.clusters.find(x => x.clusterType === 'PRIMARY');
    const encryptionAtRestEnabled =
      primaryCluster && primaryCluster.userIntent.enableEncryptionAtRest;

    // When the both the encryption enabled and rotate key values didn't change
    // we don't submit the form.
    if (encryptionAtRestEnabled === values.enableEncryptionAtRest && !values.rotateKey) {
      return false;
    }
    // When the form is submitted without changing the KMS provider select,
    // we would have the value as string otherwise it would be an object.
    const kmsProvider = isNonEmptyObject(values.selectKMSProvider) ?
      values.selectKMSProvider.value : values.selectKMSProvider;

    const data = {
      "universeUUID": universeUUID,
      "key_op": values.enableEncryptionAtRest ? "ENABLE" : "DISABLE",
      "algorithm": "AES",
      "key_size": "256",
      "kms_provider": kmsProvider
    };

    if (values.enableEncryptionAtRest && values.awsCmkPolicy) {
      readUploadedFile(values.awsCmkPolicy).then(text => {
        data.cmk_policy = text;
        handleSubmitKey(setEncryptionKey(universeUUID, data));
      });
    } else {
      handleSubmitKey(setEncryptionKey(universeUUID, data));
    }
  }

  render() {
    const { modalVisible, onHide, configList, currentUniverse } = this.props;
    const { data: {universeDetails }} = currentUniverse;
    const primaryCluster = universeDetails.clusters.find(x => x.clusterType === 'PRIMARY');
    const encryptionAtRestEnabled = primaryCluster && primaryCluster.userIntent.enableEncryptionAtRest;
    const encryptionAtRestConfig = universeDetails.encryptionAtRestConfig;
    const labelText = currentUniverse.data.name ? `Enable Encryption-at-Rest for ${this.props.name}?` : 'Enable Encryption-at-Rest?';
    const kmsOptions = [
      { value: 'auto-generated', label: 'Use auto-generated key' },
      ...configList.data.map(config => ({ value: config.provider, label: config.provider }))
    ];

    const initialValues = {
      'enableEncryptionAtRest': encryptionAtRestEnabled,
      'selectKMSProvider': null,
      'awsCmkPolicy': null,
      'rotateKey': false,
      'key_type': 'DATA_KEY'
    };

    const validationSchema = Yup.object().shape({
      enableEncryptionAtRest: Yup.boolean(),
      selectKMSProvider: Yup.mixed()
                            .when('enableEncryptionAtRest', {
                              is: true,
                              then: Yup.mixed().required('KMS Provider is required')})
    });

    if (isNonEmptyObject(encryptionAtRestConfig) && encryptionAtRestConfig.kms_provider) {
      initialValues.selectKMSProvider = encryptionAtRestConfig.kms_provider;
      initialValues.awsCmkPolicy = encryptionAtRestConfig.cmk_policy;
    }

    return (
      <Formik
        initialValues={initialValues}
        validationSchema={validationSchema}
        onSubmit={(values) => {
          this.handleSubmitForm(values);
        }}
        render={props => (
          <YBModal visible={modalVisible} formName={"EncryptionForm"} onHide={onHide} onFormSubmit={props.handleSubmit}
            submitLabel={'Submit'} cancelLabel={'Close'} showCancelButton={true} title={ "Manage Keys" }
          >
            <div className="manage-key-container">
              <Row>
                <Col lg={7}>
                  <div className="form-item-custom-label">
                    {labelText}
                  </div>
                </Col>
                <Col lg={3}>
                  <Field name="enableEncryptionAtRest" component={YBFormToggle} />
                </Col>
              </Row>
              {props.values.enableEncryptionAtRest &&
                <Row className="config-provider-row">
                  <Col lg={4}>
                    <div className="form-item-custom-label">Key Management Service Config</div>
                  </Col>
                  <Col lg={7}>
                    <Field name="selectKMSProvider" component={YBFormSelect} options={kmsOptions}/>
                  </Col>
                </Row>
              }
              {props.values.selectKMSProvider && props.values.selectKMSProvider.value === 'AWS' &&
                <Row className="config-provider-row">
                  <Col lg={11}>
                    <Field component={YBFormDropZone} name={'awsCmkPolicy'}
                      title={"Upload CM Key Policy"} className="upload-file-button"
                    />
                  </Col>
                </Row>
              }
              {encryptionAtRestEnabled && <Row>
                <Col lg={7}>
                  <div className="form-item-custom-label">
                    {"Rotate Key?"}
                  </div>
                </Col>
                <Col lg={3}>
                  <Field name="rotateKey" component={YBFormToggle} />
                </Col>
              </Row>
              }
            </div>
          </YBModal>
        )} />
    );
  }
}

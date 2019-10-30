// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { Field, Formik } from 'formik';
import 'react-bootstrap-multiselect/css/bootstrap-multiselect.css';
import { YBModal, YBFormToggle, YBFormSelect, YBFormDropZone } from '../../common/forms/fields';
import { readUploadedFile } from "../../../utils/UniverseUtils";

export default class EncryptionKeyModal extends Component {
  componentDidMount() {
    const { configList, fetchKMSConfigList } = this.props;
    if (!configList.data.length) {
      fetchKMSConfigList();
    }
  }

  handleSubmitForm = (values) => {
    const { currentUniverse: { data: { universeUUID }}, setEncryptionKey, handleSubmitKey } = this.props;
    if (values.awsCmkPolicy) {
      readUploadedFile(values.awsCmkPolicy).then(text => {
        handleSubmitKey(setEncryptionKey(
          universeUUID,
          {
            "universeUUID": universeUUID,
            "kms_provider": values.selectKMSProvider.value,
            "algorithm": "AES",
            "key_size": "256",
            "cmk_policy": text
          }
        ));
      });
    } else {
      handleSubmitKey(setEncryptionKey(
        universeUUID,
        {
          "universeUUID": universeUUID,
          "kms_provider": values.selectKMSProvider.value,
          "algorithm": "AES",
          "key_size": "256",
        }
      ));
    }
  }

  render() {
    const { modalVisible, onHide, configList, currentUniverse } = this.props;
    const primaryCluster = currentUniverse.data.universeDetails.clusters.find(x => x.clusterType === 'PRIMARY');
    const encryptionAtRestEnabled = primaryCluster && primaryCluster.userIntent.enableEncryptionAtRest;
    const labelText = currentUniverse.data.name ? `Enable Encryption-at-Rest for ${this.props.name}?` : 'Enable Encryption-at-Rest?';
    const kmsOptions = [
      { value: null, label: 'Use auto-generated key' },
      ...configList.data.map(config => ({ value: config.provider, label: config.provider }))
    ];

    const initialValues = {
      'enableEncryptionAtRest': encryptionAtRestEnabled,
      'selectKMSProvider': null,
      'awsCmkPolicy': null
    };

    return (
      <Formik
        initialValues={initialValues}
        onSubmit={(values) => {
          this.handleSubmitForm(values);
        }}
        render={props => (
          <YBModal visible={modalVisible} formName={"EncryptionForm"} onHide={onHide} onFormSubmit={props.handleSubmit}
            submitLabel={'Submit'} cancelLabel={'Close'} showCancelButton={true} title={ "Manage Keys" }
            disableSubmit={encryptionAtRestEnabled}
          >
            <div className="manage-key-container">
              <Row>
                <Col lg={7}>
                  <div className="form-item-custom-label">
                    {labelText}
                  </div>
                </Col>
                <Col lg={3}>
                  <Field name="enableEncryptionAtRest" component={YBFormToggle} isReadOnly={encryptionAtRestEnabled}/>
                </Col>
              </Row>
              {!encryptionAtRestEnabled && props.values.enableEncryptionAtRest &&
                <Row className="config-provider-row">
                  <Col lg={4}>
                    <div className="form-item-custom-label">Key Management Service Config</div>
                  </Col>
                  <Col lg={7}>
                    <Field name="selectKMSProvider" component={YBFormSelect} options={kmsOptions}/>
                  </Col>
                </Row>
              }
              {!encryptionAtRestEnabled && props.values.selectKMSProvider && props.values.selectKMSProvider.value === 'AWS' &&
                <Row className="config-provider-row">
                  <Col lg={11}>
                    <Field component={YBFormDropZone} name={'awsCmkPolicy'}
                      title={"Upload CM Key Policy"} className="upload-file-button"
                    />
                  </Col>
                </Row>
              }
            </div>
          </YBModal>
        )} />
    );
  }
}

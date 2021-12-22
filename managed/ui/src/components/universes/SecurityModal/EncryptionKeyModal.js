// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col, Alert } from 'react-bootstrap';
import { Field, Formik } from 'formik';
import * as Yup from 'yup';
import 'react-bootstrap-multiselect/css/bootstrap-multiselect.css';
import { YBModal, YBFormToggle, YBFormSelect } from '../../common/forms/fields';
import { isNonEmptyObject } from '../../../utils/ObjectUtils';

export default class EncryptionKeyModal extends Component {
  constructor(props) {
    super(props);
    this.state = {
      alertMessage: null
    };
  }

  componentDidMount() {
    const { configList, fetchKMSConfigList } = this.props;
    if (!configList.data.length) {
      fetchKMSConfigList();
    }
  }

  handleSubmitForm = (values) => {
    const {
      currentUniverse: {
        data: { universeUUID, universeDetails }
      },
      setEncryptionKey,
      handleSubmitKey,
      fetchCurrentUniverse
    } = this.props;
    const encryptionAtRestEnabled =
      universeDetails.encryptionAtRestConfig &&
      universeDetails.encryptionAtRestConfig.encryptionAtRestEnabled;

    // When both the encryption enabled and key values didn't change
    // we don't submit the form
    if (values.enableEncryptionAtRest === false && encryptionAtRestEnabled === false) {
      this.setState({ alertMessage: 'Note! No changes in configuration to submit.' });
      return false;
    }
    if (
      encryptionAtRestEnabled === values.enableEncryptionAtRest &&
      universeDetails.encryptionAtRestConfig.kmsConfigUUID === values.selectKMSProvider.value
    ) {
      this.setState({ alertMessage: 'Note! No changes in configuration to submit.' });
      return false;
    }

    const data = {
      key_op: values.enableEncryptionAtRest ? 'ENABLE' : 'DISABLE',
      kmsConfigUUID: values.enableEncryptionAtRest ? values.selectKMSProvider.value : null
    };
    setEncryptionKey(universeUUID, data).then((resp) => {
      fetchCurrentUniverse(universeDetails.universeUUID);
      handleSubmitKey(resp);
    });
  };

  render() {
    const { modalVisible, onHide, configList, currentUniverse } = this.props;
    const {
      data: { universeDetails }
    } = currentUniverse;
    const encryptionAtRestConfig = universeDetails.encryptionAtRestConfig;
    const encryptionAtRestEnabled =
      encryptionAtRestConfig && encryptionAtRestConfig.encryptionAtRestEnabled;
    const labelText = currentUniverse.data.name
      ? `Enable Encryption-at-Rest for ${this.props.name}?`
      : 'Enable Encryption-at-Rest?';
    const kmsOptions = configList?.data?.map((config) => ({
      value: config.metadata.configUUID,
      label: config.metadata.provider + ' - ' + config.metadata.name
    }));

    const initialValues = {
      enableEncryptionAtRest: encryptionAtRestEnabled,
      selectKMSProvider: isNonEmptyObject(encryptionAtRestConfig)
        ? kmsOptions.filter((option) => option.value === encryptionAtRestConfig.kmsConfigUUID)[0]
        : null,
      awsCmkPolicy: null,
      key_type: 'DATA_KEY'
    };

    const validationSchema = Yup.object().shape({
      enableEncryptionAtRest: Yup.boolean(),
      selectKMSProvider: Yup.mixed().when('enableEncryptionAtRest', {
        is: true,
        then: Yup.mixed().required('KMS Provider is required')
      })
    });

    return (
      <Formik
        initialValues={initialValues}
        enableReinitialize
        validationSchema={validationSchema}
        onSubmit={(values) => {
          this.handleSubmitForm(values);
        }}
      >
        {(props) => (
          <YBModal
            visible={modalVisible}
            formName={'EncryptionForm'}
            onHide={onHide}
            onFormSubmit={props.handleSubmit}
            submitLabel={'Submit'}
            cancelLabel={'Close'}
            showCancelButton={true}
            title="Manage Encryption at-Rest"
          >
            <div className="manage-key-container">
              <Row>
                <Col lg={7}>
                  <div className="form-item-custom-label">{labelText}</div>
                </Col>
                <Col lg={3}>
                  <Field
                    name="enableEncryptionAtRest"
                    component={YBFormToggle}
                    onChange={({ form }, event) => {
                      let alertMessage = null;
                      if (
                        event.target.checked &&
                        encryptionAtRestEnabled &&
                        form.values.selectKMSProvider.value !== encryptionAtRestConfig.kmsConfigUUID
                      ) {
                        alertMessage = 'Note! You are rotating the KMS config key.';
                      }
                      this.setState({
                        alertMessage
                      });
                    }}
                  />
                </Col>
              </Row>
              {props.values.enableEncryptionAtRest && (
                <Row className="config-provider-row">
                  <Col lg={4}>
                    <div className="form-item-custom-label">Key Management Service Config</div>
                  </Col>
                  <Col lg={7}>
                    <Field
                      name="selectKMSProvider"
                      component={YBFormSelect}
                      options={kmsOptions}
                      onChange={({ form, field, onChange }, option) => {
                        form.setFieldValue(field.name, option);
                        let alertMessage = null;
                        if (
                          encryptionAtRestEnabled &&
                          option.value !== encryptionAtRestConfig.kmsConfigUUID
                        ) {
                          alertMessage = 'Note! You are rotating the KMS config key.';
                        }
                        this.setState({
                          alertMessage
                        });
                      }}
                    />
                  </Col>
                </Row>
              )}
              {this.state.alertMessage && (
                <Row>
                  <Col lg={11}>
                    <Alert key={this.state.alertMessage} variant="warning" bsStyle="warning">
                      {this.state.alertMessage}
                    </Alert>
                  </Col>
                </Row>
              )}
            </div>
          </YBModal>
        )}
      </Formik>
    );
  }
}

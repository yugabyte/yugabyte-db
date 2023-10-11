// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { Row, Col, Alert } from 'react-bootstrap';
import { Field, Formik } from 'formik';
import * as Yup from 'yup';
import 'react-bootstrap-multiselect/css/bootstrap-multiselect.css';
import { YBModal, YBFormToggle, YBFormSelect } from '../../common/forms/fields';
import { isNonEmptyObject } from '../../../utils/ObjectUtils';
import { RBAC_ERR_MSG_NO_PERM, hasNecessaryPerm } from '../../../redesign/features/rbac/common/RbacValidator';
import { UserPermissionMap } from '../../../redesign/features/rbac/UserPermPathMapping';
import './SecurityModal.scss';

const ALERT_MESSAGES = {
  MASTER_KEY_ALERT:
    'Warning! You are rotating the master key. Restore will fail on backups taken prior to this operation.',
  UNIVERSE_KEY_ALERT: 'Submit to rotate the universe key.',
  MASTER_KEY_UNSUPPORTED: 'Note! Master Key Rotation is not supported at the moment.',
  NO_CHANGES: 'Note! No changes in configuration to submit.'
};

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
    const encryptionAtRestEnabled = universeDetails.encryptionAtRestConfig?.encryptionAtRestEnabled;

    // When both the encryption enabled and key values didn't change
    // we don't submit the form
    // when encryption is not enabled and user tries to submit without making any changes
    if (values.enableEncryptionAtRest === false && encryptionAtRestEnabled === false) {
      this.setState({ alertMessage: ALERT_MESSAGES.NO_CHANGES });
      return false;
    }

    //encryption is enabled initially and kms config is not changed ( warn only during master key rotation )
    //in the case of universe key rotation same kms config will be submitted again
    if (
      encryptionAtRestEnabled === values.enableEncryptionAtRest &&
      universeDetails.encryptionAtRestConfig.kmsConfigUUID === values.selectKMSProvider.value &&
      values.rotation_type === 'MASTER_KEY'
    ) {
      this.setState({ alertMessage: ALERT_MESSAGES.NO_CHANGES });
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
    const { modalVisible, onHide, configList, currentUniverse, featureFlags } = this.props;
    const {
      data: { universeDetails }
    } = currentUniverse;
    const encryptionAtRestConfig = universeDetails.encryptionAtRestConfig;
    const encryptionAtRestEnabled = encryptionAtRestConfig?.encryptionAtRestEnabled;
    const labelText = currentUniverse.data.name
      ? `Enable Encryption-at-Rest for ${this.props.name} ?`
      : 'Enable Encryption-at-Rest ?';
    let kmsOptions = configList?.data?.map((config) => ({
      value: config.metadata.configUUID,
      label: config.metadata.provider + ' - ' + config.metadata.name,
      id: config.metadata.provider
    }));

    //feature flagging
    const isHCVaultEnabled = featureFlags.test.enableHCVault || featureFlags.released.enableHCVault;
    if (!isHCVaultEnabled) kmsOptions = kmsOptions.filter((config) => config.id !== 'HASHICORP');
    //feature flagging

    const initialValues = {
      enableEncryptionAtRest: encryptionAtRestEnabled,
      selectKMSProvider: isNonEmptyObject(encryptionAtRestConfig)
        ? kmsOptions.filter((option) => option.value === encryptionAtRestConfig.kmsConfigUUID)[0]
        : null,
      awsCmkPolicy: null,
      key_type: 'DATA_KEY',
      rotation_type: 'UNIVERSE_KEY'
    };

    const validationSchema = Yup.object().shape({
      enableEncryptionAtRest: Yup.boolean(),
      selectKMSProvider: Yup.mixed().when('enableEncryptionAtRest', {
        is: true,
        then: Yup.mixed().required('KMS Provider is required')
      })
    });

    const canEditEAR = hasNecessaryPerm({
      onResource: universeUUID,
      ...UserPermissionMap.editEncryptionInTransit
    });

    return (
      <Formik
        initialValues={initialValues}
        enableReinitialize
        validationSchema={validationSchema}
        onSubmit={(values) => {
          this.handleSubmitForm(values);
        }}
        buttonProps={{
          primary: {
            disabled: !canEditEAR
          }
        }}
        submitButtonTooltip={!canEditEAR ? RBAC_ERR_MSG_NO_PERM : ''}
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
                        alertMessage = ALERT_MESSAGES.MASTER_KEY_ALERT;
                      }
                      this.setState({
                        alertMessage
                      });
                    }}
                  />
                </Col>
              </Row>

              {props.values.enableEncryptionAtRest && (
                <>
                  {encryptionAtRestEnabled && (
                    <Row className="config-provider-row">
                      <Col sm={4} key="universe-key-rotation" className="universe-key-op">
                        <label className="btn-group btn-group-radio">
                          <Field
                            name={'rotation_type'}
                            type="radio"
                            component="input"
                            value="UNIVERSE_KEY"
                            checked={props.values.rotation_type === 'UNIVERSE_KEY'}
                            onChange={() => {
                              props.setFieldValue(
                                'selectKMSProvider',
                                initialValues.selectKMSProvider
                              );
                              props.setFieldValue('rotation_type', 'UNIVERSE_KEY');
                              this.setState({ alertMessage: null });
                            }}
                          />
                          Rotate Universe Key
                        </label>
                      </Col>
                      <Col sm={7} key="master-key-rotation">
                        <label className="btn-group btn-group-radio master-key-rotation">
                          <Field
                            name={'rotation_type'}
                            type="radio"
                            component="input"
                            value="MASTER_KEY"
                            checked={props.values.rotation_type === 'MASTER_KEY'}
                            disabled={true}
                          />
                          Rotate Master Key
                        </label>
                      </Col>
                    </Row>
                  )}
                  <Row className="config-provider-row">
                    <Col lg={4}>
                      <div className="form-item-custom-label">Key Management Service Config</div>
                    </Col>
                    <Col lg={7}>
                      <Field
                        name="selectKMSProvider"
                        component={YBFormSelect}
                        options={kmsOptions}
                        isDisabled={
                          encryptionAtRestEnabled && props.values.rotation_type === 'UNIVERSE_KEY'
                        }
                        onChange={({ form, field }, option) => {
                          form.setFieldValue(field.name, option);
                          let alertMessage = null;
                          if (
                            encryptionAtRestEnabled &&
                            option.value !== encryptionAtRestConfig.kmsConfigUUID
                          ) {
                            alertMessage = ALERT_MESSAGES.MASTER_KEY_ALERT;
                          }
                          this.setState({
                            alertMessage
                          });
                        }}
                      />
                    </Col>
                  </Row>

                  {encryptionAtRestEnabled && props.values.rotation_type === 'UNIVERSE_KEY' && (
                    <Row>
                      <Col lg={11}>
                        <Alert
                          key={ALERT_MESSAGES.UNIVERSE_KEY_ALERT}
                          variant="info"
                          bsStyle="info"
                        >
                          {ALERT_MESSAGES.UNIVERSE_KEY_ALERT} <br />
                          {ALERT_MESSAGES.MASTER_KEY_UNSUPPORTED}
                        </Alert>
                      </Col>
                    </Row>
                  )}
                </>
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

// Copyright (c) YugaByte, Inc.

import { Component, Fragment } from 'react';
import PropTypes from 'prop-types';
import { Field } from 'formik';
import { Alert, Tabs, Tab, Row, Col } from 'react-bootstrap';
import { Box } from '@material-ui/core';

import { YBFormInput, YBFormDropZone } from '../../../common/forms/fields';
import { getPromiseState } from '../../../../utils/PromiseUtils';
import { YBModalForm } from '../../../common/forms';
import { isDefinedNotNull, isNonEmptyObject } from '../../../../utils/ObjectUtils';
import YBInfoTip from '../../../common/descriptors/YBInfoTip';
import { MODES } from './Certificates';

import './AddCertificateForm.scss';

export default class AddCertificateForm extends Component {
  static propTypes = {
    backupInfo: PropTypes.object
  };

  state = {
    tab: 'selfSigned',
    suggestionText: {
      rootCACert: '',
      nodeCertPath: '',
      nodeCertPrivate: '',
      clientCertPath: '',
      clientKeyPath: ''
    },
    isDatePickerFocused: false
  };

  placeholderObject = {
    rootCACert: '/opt/yugabyte/keys/cert1/ca.crt',
    nodeCertPath: '/opt/yugabyte/keys/cert1/node.crt',
    nodeCertPrivate: '/opt/yugabyte/keys/cert1/node.key',
    clientCertPath: '/opt/yugabyte/yugaware/data/cert1/client.crt',
    clientKeyPath: '/opt/yugabyte/yugaware/data/cert1/client.key'
  };

  readUploadedFileAsText = (inputFile, isRequired) => {
    const fileReader = new FileReader();
    return new Promise((resolve) => {
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

  addCertificate = (vals, setSubmitting) => {
    const self = this;
    const certificateFile = vals.certContent;
    if (this.state.tab === 'selfSigned') {
      const keyFile = vals.keyContent;
      const formValues = {
        label: vals.certName,
        certType: 'SelfSigned'
      };
      const fileArray = [
        this.readUploadedFileAsText(certificateFile, false),
        this.readUploadedFileAsText(keyFile, false)
      ];

      // Catch all onload events for configs
      Promise.all(fileArray).then(
        (files) => {
          formValues.certContent = files[0];
          formValues.keyContent = files[1];
          self.props.addCertificate(formValues, setSubmitting);
        },
        (reason) => {
          console.warn('File Upload gone wrong', reason);
          setSubmitting(false);
        }
      );
    } else if (this.state.tab === 'caSigned') {
      const formValues = {
        label: vals.certName,
        certType: 'CustomCertHostPath',
        customCertInfo: {
          nodeCertPath: vals.nodeCertPath,
          nodeKeyPath: vals.nodeCertPrivate,
          rootCertPath: vals.rootCACert,
          clientCertPath: vals.clientCertPath,
          clientKeyPath: vals.clientKeyPath
        }
      };

      this.readUploadedFileAsText(certificateFile, false)
        .then((content) => {
          formValues.certContent = content;
          self.props.addCertificate(formValues, setSubmitting);
        })
        .catch((err) => {
          console.warn(`File Upload gone wrong. ${err}`);
          setSubmitting(false);
        });
    } else if (this.state.tab === 'k8s') {
      const formValues = {
        label: vals.certName,
        certType: 'K8SCertManager'
      };

      this.readUploadedFileAsText(certificateFile, false)
        .then((content) => {
          formValues.certContent = content;
          self.props.addCertificate(formValues, setSubmitting);
        })
        .catch((err) => {
          console.warn(`File Upload gone wrong. ${err}`);
          setSubmitting(false);
        });
    } else if (this.state.tab === 'hashicorp') {
      const formValues = {
        label: vals.certName,
        certType: 'HashicorpVault',
        hcVaultCertParams: {
          vaultAddr: vals.vaultAddr,
          vaultToken: vals.vaultToken,
          mountPath: vals.mountPath ?? 'pki/',
          role: vals.role,
          engine: 'pki'
        },
        certContent: 'pki'
      };
      self.props.addCertificate(formValues, setSubmitting);
    }
  };

  updateCertificate = (vals, setSubmitting) => {
    const self = this;
    if (this.state.tab === 'hashicorp') {
      const formValues = {
        label: vals.certName,
        certType: 'HashicorpVault',
        hcVaultCertParams: {
          vaultToken: vals.vaultToken
        },
        certContent: 'pki'
      };
      self.props.updateCertificate(vals.uuid, formValues, setSubmitting);
    }
  };

  validateForm = (values) => {
    const errors = {};
    if (!values.certName) {
      errors.certName = 'Certificate name is required';
    }

    if (this.state.tab !== 'hashicorp' && !values.certContent) {
      errors.certContent = 'Certificate file is required';
    }

    if (this.state.tab === 'selfSigned') {
      if (!values.keyContent) {
        errors.keyContent = 'Key file is required';
      }
    } else if (this.state.tab === 'caSigned') {
      if (!values.rootCACert) {
        errors.rootCACert = 'Root CA certificate is required';
      }
      if (!values.nodeCertPath) {
        errors.nodeCertPath = 'Database node certificate path is required';
      }
      if (!values.nodeCertPrivate) {
        errors.nodeCertPrivate = 'Database node certificate private key is required';
      }
    } else if (this.state.tab === 'hashicorp') {
      if (!values.vaultToken) {
        errors.vaultToken = 'Secret Token is Required';
      }

      if (!values.role) {
        errors.role = 'Role is Required';
      }

      if (!values.vaultAddr) {
        errors.vaultAddr = 'Vault Address is Required';
      } else {
        const exp = new RegExp(/^(?:http(s)?:\/\/)?[\w.-]+:\d{1,5}(\/)?$/);
        if (!exp.test(values.vaultAddr))
          errors.vaultAddr = 'Vault Address must be a valid URL with port number';
      }
    }
    return errors;
  };

  onHide = () => {
    this.props.onHide();
    this.props.addCertificateReset();
  };

  tabSelect = (newTabKey, formikProps) => {
    const { setFieldTouched, setFieldValue, setErrors, errors } = formikProps;
    const newErrors = { ...errors };
    if (this.state.tab !== newTabKey && newTabKey === 'selfSigned') {
      setFieldValue('rootCACert', '');
      setFieldValue('nodeCertPath', '');
      setFieldValue('nodeCertPrivate', '');
      setFieldValue('clientCertPath', '');
      setFieldValue('clientKeyPath', '', false);

      delete newErrors.rootCACert;
      delete newErrors.nodeCertPath;
      delete newErrors.nodeCertPrivate;
    } else if (this.state.tab !== newTabKey && newTabKey === 'caSigned') {
      setFieldValue('keyContent', null, false);
      delete newErrors.keyContent;
    }
    setFieldTouched('keyContent', false);
    setFieldTouched('rootCACert', false);
    setFieldTouched('nodeCertPath', false);
    setFieldTouched('nodeCertPrivate', false);
    setFieldTouched('clientCertPath', false);
    setFieldTouched('clientKeyPath', false);
    setErrors(newErrors);
    this.setState({ tab: newTabKey });
  };

  handleOnBlur = (event) => {
    this.setState({
      ...this.state,
      suggestionText: {
        [event.target.name]: ''
      }
    });
  };

  handleOnKeyUp = (event, formikProps) => {
    const { setFieldValue } = formikProps;
    const value = event.target.value;
    const name = event.target.name;
    const regex = new RegExp('^' + value, 'i');
    const term = this.placeholderObject[name];
    if (event.key === 'ArrowRight' && this.state.suggestionText[name]) {
      setFieldValue(name, term);
      this.setState({
        ...this.state,
        suggestionText: {
          [name]: ''
        }
      });
      return false;
    }
    if (regex.test(term) && value) {
      this.setState({
        ...this.state,
        suggestionText: {
          [name]: `${value + term.slice(value.length)}`
        }
      });
      return false;
    }
    this.setState({
      ...this.state,
      suggestionText: {
        [name]: ''
      }
    });
  };

  getHCVaultForm = () => {
    const { mode } = this.props;
    const isEditMode = mode === MODES.EDIT;
    return (
      <Fragment>
        <Row className="hc-field-c">
          <Col className="hc-label-c">
            <div>Config Name</div>
          </Col>
          <Col>
            <Field disabled={isEditMode} name="certName" component={YBFormInput} />
          </Col>
        </Row>

        <Row className="hc-field-c">
          <Col className="hc-label-c">
            <div>
              Vault Address&nbsp;
              <YBInfoTip
                title="Vault Address"
                content="Vault Address must be a valid URL with port number, Ex:- http://0.0.0.0:0000"
              >
                <i className="fa fa-info-circle" />
              </YBInfoTip>
            </div>
          </Col>
          <Col>
            <Field disabled={isEditMode} name={'vaultAddr'} component={YBFormInput} />
          </Col>
        </Row>

        <Row className="hc-field-c">
          <Col className="hc-label-c">
            <div>Secret Token</div>
          </Col>
          <Col>
            <Field name={'vaultToken'} component={YBFormInput} />
          </Col>
        </Row>

        <Row className="hc-field-c">
          <Col className="hc-label-c">
            <div>Secret Engine</div>
          </Col>
          <Col>
            <Field name={'engine'} value="pki" disabled={true} component={YBFormInput} />
          </Col>
        </Row>

        <Row className="hc-field-c">
          <Col className="hc-label-c">
            <div>Role</div>
          </Col>
          <Col>
            <Field disabled={isEditMode} name={'role'} component={YBFormInput} />
          </Col>
        </Row>

        <Row className="hc-field-c">
          <Col className="hc-label-c">
            <div>
              Mount Path&nbsp;
              <YBInfoTip
                title="Mount Path"
                content="Enter the mount path. If mount path is not specified, path will be auto set to 'pki/'"
              >
                <i className="fa fa-info-circle" />
              </YBInfoTip>
            </div>
          </Col>
          <Col>
            <Field
              disabled={isEditMode}
              name={'mountPath'}
              placeholder={'pki/'}
              component={YBFormInput}
            />
          </Col>
        </Row>
      </Fragment>
    );
  };

  getInitValues = () => {
    const { mode, certificate } = this.props;
    const isEditMode = mode === MODES.EDIT;
    const initialValues = {
      certName: '',
      certContent: null,
      keyContent: null,
      rootCACert: '',
      nodeCertPath: '',
      nodeCertPrivate: '',
      clientCertPath: '',
      clientKeyPath: ''
    };

    if (isEditMode && certificate.type === 'HashicorpVault') {
      return {
        ...certificate.hcVaultCertParams,
        uuid: certificate.uuid,
        certName: certificate.name
      };
    }

    return initialValues;
  };

  UNSAFE_componentWillReceiveProps() {
    const { certificate, mode } = this.props;
    const isEditMode = mode === MODES.EDIT;
    if (isEditMode && certificate.type === 'HashicorpVault') this.setState({ tab: 'hashicorp' });
  }

  render() {
    const {
      customer: { addCertificate },
      isHCVaultEnabled,
      mode
    } = this.props;
    const isEditMode = mode === MODES.EDIT;

    return (
      <div className="add-cert-modal">
        <YBModalForm
          title={isEditMode ? 'Edit Certificate' : 'Add Certificate'}
          className={getPromiseState(addCertificate).isError() ? 'modal-shake' : ''}
          visible={this.props.visible}
          onHide={this.onHide}
          showCancelButton={true}
          submitLabel={isEditMode ? 'Save' : 'Add'}
          cancelLabel="Cancel"
          onFormSubmit={(values, { setSubmitting }) => {
            setSubmitting(true);
            const payload = {
              ...values,
              label: values.certName.trim()
            };
            isEditMode
              ? this.updateCertificate(payload, setSubmitting)
              : this.addCertificate(payload, setSubmitting);
          }}
          initialValues={this.getInitValues()}
          validate={this.validateForm}
          render={(props) => {
            return (
              <Tabs
                id="add-cert-tabs"
                activeKey={this.state.tab}
                onSelect={(k) => this.tabSelect(k, props)}
              >
                {!isEditMode && (
                  <Tab eventKey="selfSigned" title="Self Signed">
                    <Field
                      name="certName"
                      component={YBFormInput}
                      type="text"
                      label="Certificate Name"
                      required
                    />
                    <Box display="flex" flexDirection="column" gridGap="10px">
                      <Field
                        name="certContent"
                        component={YBFormDropZone}
                        title="Upload Root Certificate"
                        required
                      />
                      <Field
                        name="keyContent"
                        component={YBFormDropZone}
                        title="Upload Key"
                        required
                      />
                    </Box>

                    {getPromiseState(addCertificate).isError() &&
                      isNonEmptyObject(addCertificate.error) && (
                        <Alert bsStyle="danger" variant="danger">
                          Certificate adding has been failed:
                          <br />
                          {JSON.stringify(addCertificate.error)}
                        </Alert>
                      )}
                  </Tab>
                )}
                {!isEditMode && (
                  <Tab eventKey="caSigned" title="CA Signed">
                    <Field
                      name="certName"
                      component={YBFormInput}
                      type="text"
                      label="Certificate Name"
                      required
                    />
                    <Field
                      name="certContent"
                      component={YBFormDropZone}
                      title="Upload Root Certificate"
                      required
                    />
                    <div className="search-container">
                      <Field
                        name="rootCACert"
                        component={YBFormInput}
                        label="Root CA Certificate"
                        placeholder={this.placeholderObject['rootCACert']}
                        required
                        onKeyUp={(e) => this.handleOnKeyUp(e, props)}
                        onBlur={this.handleOnBlur}
                        className={this.state.isDatePickerFocused ? null : 'search'}
                      />
                      <div className="suggestion">{this.state.suggestionText['rootCACert']}</div>
                    </div>
                    <div className="search-container">
                      <Field
                        name="nodeCertPath"
                        component={YBFormInput}
                        label="Database Node Certificate Path"
                        placeholder={this.placeholderObject['nodeCertPath']}
                        required
                        onKeyUp={(e) => this.handleOnKeyUp(e, props)}
                        onBlur={this.handleOnBlur}
                        className={this.state.isDatePickerFocused ? null : 'search'}
                      />
                      <div className="suggestion">{this.state.suggestionText['nodeCertPath']}</div>
                    </div>
                    <div className="search-container">
                      <Field
                        name="nodeCertPrivate"
                        component={YBFormInput}
                        label="Database Node Certificate Private Key"
                        placeholder={this.placeholderObject['nodeCertPrivate']}
                        required
                        onKeyUp={(e) => this.handleOnKeyUp(e, props)}
                        onBlur={this.handleOnBlur}
                        className="search"
                      />
                      <div className="suggestion">
                        {this.state.suggestionText['nodeCertPrivate']}
                      </div>
                    </div>
                    <div className="search-container">
                      <Field
                        name="clientCertPath"
                        component={YBFormInput}
                        label="Client Certificate"
                        placeholder={this.placeholderObject['clientCertPath']}
                        onKeyUp={(e) => this.handleOnKeyUp(e, props)}
                        onBlur={this.handleOnBlur}
                        className="search"
                      />
                      <div className="suggestion">
                        {this.state.suggestionText['clientCertPath']}
                      </div>
                    </div>
                    <div className="search-container">
                      <Field
                        name="clientKeyPath"
                        component={YBFormInput}
                        label="Client Certificate Private Key"
                        placeholder={this.placeholderObject['clientKeyPath']}
                        onKeyUp={(e) => this.handleOnKeyUp(e, props)}
                        onBlur={this.handleOnBlur}
                        className="search"
                      />
                      <div className="suggestion">{this.state.suggestionText['clientKeyPath']}</div>
                    </div>
                    {getPromiseState(addCertificate).isError() &&
                      isNonEmptyObject(addCertificate.error) && (
                        <Alert bsStyle="danger" variant="danger">
                          Certificate adding has been failed:
                          <br />
                          {JSON.stringify(addCertificate.error)}
                        </Alert>
                      )}
                  </Tab>
                )}

                {isHCVaultEnabled && (
                  <Tab eventKey="hashicorp" title="Hashicorp">
                    {this.getHCVaultForm()}
                  </Tab>
                )}

                {!isEditMode && (
                  <Tab eventKey="k8s" title="K8S cert-manager">
                    <Field
                      name="certName"
                      component={YBFormInput}
                      type="text"
                      label="Certificate Name"
                      required
                    />

                    <Field
                      name="certContent"
                      component={YBFormDropZone}
                      title="Upload Root Certificate"
                      required
                    />

                    {getPromiseState(addCertificate).isError() &&
                      isNonEmptyObject(addCertificate.error) && (
                        <Alert bsStyle="danger" variant="danger">
                          Certificate adding has been failed:
                          <br />
                          {JSON.stringify(addCertificate.error)}
                        </Alert>
                      )}
                  </Tab>
                )}
              </Tabs>
            );
          }}
        />
      </div>
    );
  }
}

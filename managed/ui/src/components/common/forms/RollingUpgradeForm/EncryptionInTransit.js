import { Field } from 'formik';
import React from 'react';
import { Alert, Col, Row } from 'react-bootstrap';
import { YBCheckBox, YBControlledSelectWithLabel, YBFormInput, YBFormToggle, YBToggle } from '../fields';
import YBModalForm from '../YBModalForm/YBModalForm';

import { YBLoading } from '../../indicators';
import { getPromiseState } from '../../../../utils/PromiseUtils';
import { useDispatch, useSelector } from 'react-redux';
import { getPrimaryCluster, getReadOnlyCluster } from '../../../../utils/UniverseUtils';
import { updateTLS } from '../../../../actions/customers';
import './EncryptionInTransit.scss';

const CLIENT_TO_NODE_ROTATE_MSG = "Note! Changing the client to node root certificate may cause the client to loose connection, please update the certificate used in your client application.";
const NODE_TO_NODE_ROTATE_MSG = "Note! You are changing the node to node root certificate."

const generateSelectOptions = (certificates) => {
  let options = certificates.map((cert) => (
    <option key={cert.uuid} value={cert.uuid}>
      {cert.label}
    </option>
  ));
  options = [
    <option key={'disabled'} disabled value={'no-certificate-present'}>
      -- select a certificate --
    </option>,
    ...options
  ];
  return options;
};

function disableUniverseEncryption(values, inputName, currentValue, setFieldValue) {
  const otherInputName =
    inputName === 'enableNodeToNodeEncrypt'
      ? 'enableClientToNodeEncrypt'
      : 'enableNodeToNodeEncrypt';

  if (!currentValue && !values[otherInputName]) {
    setFieldValue('enableUniverseEncryption', false);
  }
}

function getEncryptionComponent(
  { inputName, label, values, status, setFieldValue, rotateCertMsg },
  { selectName, selectLabel, selectOptions, handleSelectChange, disabled }
) {
  const options = generateSelectOptions(selectOptions);
  return (
    <>
      <Row>
        <Col lg={5}>
          <div className="form-item-custom-label">{label}</div>
        </Col>
        <Col lg={7}>
          <Field 
            name={inputName}
            component={YBFormToggle}
            onChange={(_, e)=> {
              disableUniverseEncryption(values, inputName, e.target.checked, setFieldValue);
            }}
          />
        </Col>
      </Row>
      {values[inputName] && (
        <Row>
          <Col lg={12}>
            <Field
              name={selectName}
              component={YBControlledSelectWithLabel}
              onInputChanged={handleSelectChange}
              input={{ name: selectName }}
              selectVal={
                values[selectName] !== null ? values[selectName] : 'no-certificate-present'
              }
              options={options}
              label={selectLabel}
              width="100%"
              isReadOnly={disabled}
            />
          </Col>
          <Col lg={12}>
            {rotateCertMsg && (
              <Alert
                className="rotate-alert-msg"
                key={inputName + "-rotate-msg"}
                variant="warning"
                bsStyle="warning"
              >
                <i class="fa fa-exclamation-triangle" aria-hidden="true"></i>
                &nbsp;&nbsp;{rotateCertMsg}
              </Alert>
            )}
          </Col>
        </Row>
      )}
      {status && status[inputName] && (
        <Alert key={status[inputName]} variant="warning" bsStyle="warning">
          {status[inputName]}
        </Alert>
      )}
    </>
  );
}

export function EncryptionInTransit({ visible, onHide, currentUniverse }) {
  const userCertificates = useSelector((state) => state.customer.userCertificates);

  const isCertificateListLoading =
    getPromiseState(userCertificates).isInit() || getPromiseState(userCertificates).isLoading();

  const dispatch = useDispatch();

  const cluster =
    currentUniverse.data.universeDetails.currentClusterType === 'PRIMARY'
      ? getPrimaryCluster(currentUniverse.data.universeDetails.clusters)
      : getReadOnlyCluster(currentUniverse.data.universeDetails.clusters);

  const { universeDetails } = currentUniverse.data;

  const initialValues = {
    enableUniverseEncryption:
      cluster.userIntent.enableNodeToNodeEncrypt || cluster.userIntent.enableClientToNodeEncrypt,
    enableNodeToNodeEncrypt: cluster.userIntent.enableNodeToNodeEncrypt,
    enableClientToNodeEncrypt: cluster.userIntent.enableClientToNodeEncrypt,
    rootCA: universeDetails.rootCA,
    clientRootCA: universeDetails.clientRootCA,
    rootAndClientRootCASame: universeDetails.rootAndClientRootCASame,
    timeDelay: 240,
    rollingUpgrade: true
  };

  const preparePayload = (formValues, setStatus) => {
    setStatus();

    if (!formValues.enableUniverseEncryption) {
      return {
        enableClientToNodeEncrypt: false,
        enableNodeToNodeEncrypt: false,
        rootCA: null,
        clientRootCA: null,
        rootAndClientRootCASame: false
      };
    }

    if (formValues.enableNodeToNodeEncrypt && formValues.rootCA === null) {
        setStatus({
          enableNodeToNodeEncrypt: 'Select a Root CA'
        });
        return;
    }

    if (formValues.rootAndClientRootCASame && !formValues.enableNodeToNodeEncrypt) {
      setStatus({
        enableNodeToNodeEncrypt: 'Node to Node must be enabled to use same certificate'
      });
      return;
    }
    if (formValues.enableClientToNodeEncrypt === true && !formValues.rootAndClientRootCASame) {
      if (formValues.clientRootCA === null) {
        setStatus({
          enableClientToNodeEncrypt: 'Select a Root CA'
        });
        return;
      }
    }

    if (formValues.enableNodeToNodeEncrypt === false) {
      formValues['rootCA'] = null;
    }
    if (formValues.enableClientToNodeEncrypt === false) {
      formValues['clientRootCA'] = null;
    }

    if (formValues.rootAndClientRootCASame) {
      if (formValues.enableNodeToNodeEncrypt && formValues.enableClientToNodeEncrypt) {
        formValues['clientRootCA'] = formValues['rootCA'];
      }
    }

    return formValues;
  };

  const handleSubmit = (formValues, setStatus) => {
    const payload = preparePayload(formValues, setStatus);
    if (!payload) {
      return;
    }

    dispatch(updateTLS(currentUniverse.data.universeDetails.universeUUID, payload)).then((resp) => {
      if (resp.error) {
        setStatus({ error: resp.payload.response.data.error });
      } else {
        onHide();
      }
    });
  };

  return (
    <YBModalForm
      visible={visible}
      onHide={onHide}
      showCancelButton={true}
      title="TLS Configuration"
      initialValues={initialValues}
      className={getPromiseState(userCertificates).isError() ? 'modal-shake' : ''}
      onFormSubmit={(values, { setSubmitting, setStatus }) => {
        handleSubmit(values, setStatus);
        setSubmitting(false);
      }}
      render={({ values, handleChange, setFieldValue, status, setStatus }) => {
        if (isCertificateListLoading) {
          return <YBLoading />;
        }

        return (
          <div className="encryption-in-transit">
            <Row className="enable-universe">
              <Col lg={10}>
                <div className="form-item-custom-label">
                  <b>{'Encryption in Transit for this Universe'}</b>
                </div>
              </Col>
              <Col lg={2}>
                <Field name="enableUniverseEncryption">
                  {({ field }) => (
                    <YBToggle
                      onToggle={(e) => {
                        e.target.value && setFieldValue('enableNodeToNodeEncrypt', true);
                      }}
                      name="enableUniverseEncryption"
                      input={{
                        value: field.value,
                        onChange: field.onChange
                      }}
                    />
                  )}
                </Field>
              </Col>
            </Row>
            {status?.error && (
              <Row className="err-msg">
                <Col lg={12}>
                  <Alert
                    key={'error'}
                    variant="warning"
                    bsStyle="warning"
                    onDismiss={() => setStatus()}
                  >
                    {status.error}
                  </Alert>
                </Col>
              </Row>
            )}
            {!values.enableUniverseEncryption ? null : (
              <>
                <Row className="use-same-certificate">
                  <Col lg={12}>
                    <Field name="rootAndClientRootCASame">
                      {({ field }) => (
                        <YBCheckBox
                          label={
                            <span style={{ fontWeight: 'normal' }}>
                              Use the same certificate for node to node and client to node
                              encryption
                            </span>
                          }
                          name="rootAndClientRootCASame"
                          input={{
                            checked: values['enableClientToNodeEncrypt'] && field.value,
                            onChange: (e) => {
                              setFieldValue('rootAndClientRootCASame', e.target.checked);

                              if (e.target.checked) {
                                setFieldValue('enableClientToNodeEncrypt', true);
                                setFieldValue('clientRootCA', values['rootCA']);
                              }
                            }
                          }}
                        />
                      )}
                    </Field>
                  </Col>
                </Row>
                <div className="certificates-area">
                  {getEncryptionComponent(
                    {
                      inputName: 'enableNodeToNodeEncrypt',
                      label: <b>Node to Node Encryption</b>,
                      values,
                      status,
                      setStatus,
                      setFieldValue,
                      rotateCertMsg:
                        initialValues["rootCA"] !== values["rootCA"]
                          ? NODE_TO_NODE_ROTATE_MSG
                          : null
                    },
                    {
                      selectName: 'rootCA',
                      selectLabel: 'Select a root certificate',
                      selectOptions: userCertificates.data,
                      handleSelectChange: handleChange
                    }
                  )}
                  {getEncryptionComponent(
                    {
                      inputName: 'enableClientToNodeEncrypt',
                      label: <b>Client to Node Encryption</b>,
                      values,
                      status,
                      setStatus,
                      setFieldValue,
                      rotateCertMsg:
                        initialValues["clientRootCA"] !== values["clientRootCA"]
                          ? CLIENT_TO_NODE_ROTATE_MSG
                          : null
                    },
                    {
                      selectName: 'clientRootCA',
                      disabled: values['rootAndClientRootCASame'],
                      selectLabel: 'Select a client Certificate',
                      selectOptions: userCertificates.data,
                      handleSelectChange: handleChange
                    }
                  )}
                </div>
                <Row className="rolling-upgrade">
                  <Col lg={12}>
                    <Field 
                      name="rollingUpgrade"
                      component={YBCheckBox}
                      checkState={initialValues.rollingUpgrade}
                      label="Rolling Upgrade"
                    />
                  </Col>
                </Row>
                <Row className="server-delay">
                  <Col lg={12}>
                    <Field 
                      name="timeDelay"
                      type="number"
                      label="Upgrade Delay Between Servers (seconds)"
                      component={YBFormInput}
                    />
                  </Col>
                </Row>
              </>
            )}
          </div>
        );
      }}
    ></YBModalForm>
  );
}

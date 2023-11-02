import { Field } from 'formik';
import { Alert, Col, Row } from 'react-bootstrap';
import { YBCheckBox, YBControlledSelectWithLabel, YBFormToggle, YBToggle } from '../fields';
import YBModalForm from '../YBModalForm/YBModalForm';

import { YBLoading } from '../../indicators';
import { getPromiseState } from '../../../../utils/PromiseUtils';
import { useDispatch, useSelector } from 'react-redux';
import { getPrimaryCluster, getReadOnlyCluster } from '../../../../utils/UniverseUtils';
import { updateTLS } from '../../../../actions/customers';
import { YBBanner, YBBannerVariant } from '../../descriptors';
import { hasLinkedXClusterConfig } from '../../../xcluster/ReplicationUtils';

import { hasNecessaryPerm } from '../../../../redesign/features/rbac/common/RbacValidator';
import { UserPermissionMap } from '../../../../redesign/features/rbac/UserPermPathMapping';
import './EncryptionInTransit.scss';

const CLIENT_TO_NODE_ROTATE_MSG =
  'Note! Changing the client to node root certificate may cause the client to loose connection, please update the certificate used in your client application.';
const NODE_TO_NODE_ROTATE_MSG = 'Note! You are changing the node to node root certificate.';

const CREATE_NEW_CERTIFICATE = 'create-new-certificate';

const generateSelectOptions = (certificates) => {
  let options = certificates.map((cert) => (
    <option key={cert.uuid} value={cert.uuid}>
      {cert.label}
    </option>
  ));
  options = [
    <option key={CREATE_NEW_CERTIFICATE} value={CREATE_NEW_CERTIFICATE}>
      -- Create a new certificate --
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
            onChange={(_, e) => {
              disableUniverseEncryption(values, inputName, e.target.checked, setFieldValue);
              //If NodeToNode is disabled, disable rootAndClientRootCASame
              if (inputName === 'enableNodeToNodeEncrypt' && !e.target.checked) {
                setFieldValue('rootAndClientRootCASame', false);
              }
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
                values[selectName] !== undefined ? values[selectName] : CREATE_NEW_CERTIFICATE
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
                key={inputName + '-rotate-msg'}
                variant="warning"
                bsStyle="warning"
              >
                <i className="fa fa-exclamation-triangle" aria-hidden="true"></i>
                &nbsp;&nbsp;{rotateCertMsg}
              </Alert>
            )}
          </Col>
        </Row>
      )}
      {status?.[inputName] && (
        <Alert key={status[inputName]} variant="warning" bsStyle="warning">
          {status[inputName]}
        </Alert>
      )}
    </>
  );
}

export function EncryptionInTransit({ visible, onHide, currentUniverse, fetchCurrentUniverse }) {
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
    rootCA: universeDetails.rootCA ?? CREATE_NEW_CERTIFICATE,
    clientRootCA: universeDetails.clientRootCA
      ? universeDetails.clientRootCA
      : universeDetails.rootAndClientRootCASame
      ? universeDetails.rootCA
      : CREATE_NEW_CERTIFICATE,
    createNewRootCA: false,
    createNewClientRootCA: false,
    rootAndClientRootCASame: universeDetails.rootAndClientRootCASame,
    rollingUpgrade: false
  };

  const preparePayload = (formValues, setStatus) => {
    setStatus();

    if (!formValues.enableUniverseEncryption) {
      return {
        enableClientToNodeEncrypt: false,
        enableNodeToNodeEncrypt: false,
        rootCA: null,
        createNewRootCA: false,
        clientRootCA: null,
        createNewClientRootCA: false,
        rootAndClientRootCASame: false
      };
    }

    if (formValues.rootAndClientRootCASame && !formValues.enableNodeToNodeEncrypt) {
      setStatus({
        enableNodeToNodeEncrypt: 'Node to Node must be enabled to use same certificate'
      });
      return;
    }

    if (formValues.enableNodeToNodeEncrypt === false) {
      formValues['rootCA'] = null;
      formValues['createNewRootCA'] = false;
    }
    if (formValues.enableNodeToNodeEncrypt === true) {
      if (formValues['rootCA'] === CREATE_NEW_CERTIFICATE) {
        formValues['rootCA'] = null;
        formValues['createNewRootCA'] = true;
      } else {
        formValues['createNewRootCA'] = false;
      }
    }

    if (formValues.enableClientToNodeEncrypt === false) {
      formValues['clientRootCA'] = null;
      formValues['createNewClientRootCA'] = false;
    }
    if (formValues.enableClientToNodeEncrypt === true && !formValues.rootAndClientRootCASame) {
      if (formValues['clientRootCA'] === CREATE_NEW_CERTIFICATE) {
        formValues['clientRootCA'] = null;
        formValues['createNewClientRootCA'] = true;
      } else {
        formValues['createNewClientRootCA'] = false;
      }
    }

    if (formValues.rootAndClientRootCASame) {
      if (formValues.enableNodeToNodeEncrypt && formValues.enableClientToNodeEncrypt) {
        formValues['clientRootCA'] = null;
        formValues['createNewClientRootCA'] = false;
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
        fetchCurrentUniverse(currentUniverse.data.universeDetails.universeUUID);
        onHide();
      }
    });
  };

  const universeHasXClusterConfig = hasLinkedXClusterConfig([currentUniverse.data]);
  const canEditEAT = hasNecessaryPerm({
    onResource: currentUniverse.data.universeDetails.universeUUID,
    ...UserPermissionMap.editEncryptionInTransit
  });

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
      isButtonDisabled={!canEditEAT}
      render={({ values, handleChange, setFieldValue, status, setStatus }) => {
        if (isCertificateListLoading) {
          return <YBLoading />;
        }

        return (
          <div className="encryption-in-transit">
            {universeHasXClusterConfig && (
              <YBBanner variant={YBBannerVariant.WARNING} showBannerIcon={false}>
                <b>{`Warning! `}</b>
                <p>
                  This universe is involved in one or more xCluster configurations. Toggling TLS on
                  this universe will break the xCluster replication.
                </p>
                <p>
                  To enable replication again after toggling TLS on this universe, you must:
                  <ol>
                    <li>
                      Configure the TLS toggle on all other participating universes to match the
                      current universe.
                    </li>
                    <li>Restart all affected xCluster configurations.</li>
                  </ol>
                </p>
              </YBBanner>
            )}
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
                          disabled={!values['enableNodeToNodeEncrypt']}
                          label={
                            <span style={{ fontWeight: 'normal' }}>
                              Use the same certificate for node to node and client to node
                              encryption
                            </span>
                          }
                          name="rootAndClientRootCASame"
                          input={{
                            checked:
                              values['enableClientToNodeEncrypt'] &&
                              values['enableNodeToNodeEncrypt'] &&
                              field.value,
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
                        initialValues['rootCA'] !== values['rootCA']
                          ? NODE_TO_NODE_ROTATE_MSG
                          : null
                    },
                    {
                      selectName: 'rootCA',
                      selectLabel: 'Select a root certificate',
                      selectOptions: userCertificates.data,
                      handleSelectChange: (e) => {
                        setFieldValue('rootCA', e.target.value);
                        if (values['rootAndClientRootCASame']) {
                          setFieldValue('clientRootCA', e.target.value);
                        }
                      }
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
                        initialValues['clientRootCA'] !== values['clientRootCA']
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
              </>
            )}
          </div>
        );
      }}
    ></YBModalForm>
  );
}

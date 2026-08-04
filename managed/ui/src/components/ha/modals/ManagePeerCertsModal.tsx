import { useRef } from 'react';
import { Field, FieldArray, Form, FormikProps } from 'formik';

import { YBModalForm } from '../../common/forms';
import { YBButton } from '../../common/forms/fields';
import { PeerCert } from '../replication/HAReplicationView';

import styles from './ManagePeerCertsModal.module.scss';

interface ManagePeerCertsModalProps {
  visible: boolean;
  peerCerts: PeerCert[];
  setYBHAWebserviceRuntimeConfig: (value: string) => void;
  onClose: () => void;
}

interface CertFormItem {
  data: string;
}

interface FormValues {
  peerCerts: CertFormItem[];
}

export const PEER_CERT_PREFIX = '-----BEGIN CERTIFICATE-----';
export const PEER_CERT_SUFFIX = '-----END CERTIFICATE-----';

/**
 * Format certificate information from input form to match the expectation of yb.ha.ws
 * Reference:
 * (Runtime config) yugabyte-db/managed/src/main/resources/reference.conf
 * (Play-ws) https://github.com/playframework/play-ws/blob/main/play-ws-standalone/src/main/resources/reference.conf
 */
const formatYBHAWebSerivceConfigValue = (peerCerts: CertFormItem[]) =>
  `{
    ssl {
      trustManager = {
        ${peerCerts
          .map(
            (peerCert) => `stores += {
          type = "PEM"
          data = """${peerCert.data}"""
        }
        `
          )
          .join('')}
      }
    }  
  }`;

const adaptPeerCertsToFormValues = (peerCerts: PeerCert[]): FormValues => {
  return {
    peerCerts: peerCerts.map((peerCert) => ({
      data: peerCert.data
    }))
  };
};

const validatePeerCertData = (data: string) => {
  let error;
  if (!data.startsWith(PEER_CERT_PREFIX) || !data.endsWith(PEER_CERT_SUFFIX)) {
    error = `Peer certs must be:
     - prefixed with "${PEER_CERT_PREFIX}" AND
     - suffixed with "${PEER_CERT_SUFFIX}"`;
  }
  return error;
};

const validateForm = (values: FormValues) => {
  const errors: Partial<FormValues> = {};

  const peerCertsErrors = Array(values.peerCerts.length).fill(undefined);
  let hasPeerCertError = false;
  values.peerCerts.forEach((peerCert, index) => {
    const fieldError = validatePeerCertData(peerCert.data);
    if (fieldError) {
      hasPeerCertError = true;
      peerCertsErrors[index] = { data: fieldError, type: undefined };
    }
  });

  if (hasPeerCertError) {
    errors.peerCerts = peerCertsErrors;
  }
  return errors;
};

export const ManagePeerCertsModal = ({
  visible,
  peerCerts,
  setYBHAWebserviceRuntimeConfig,
  onClose
}: ManagePeerCertsModalProps) => {
  const formik = useRef({} as FormikProps<FormValues>);

  const closeModal = () => {
    if (!formik.current.isSubmitting) {
      onClose();
    }
  };

  const submitForm = async (values: FormValues) => {
    await setYBHAWebserviceRuntimeConfig(formatYBHAWebSerivceConfigValue(values.peerCerts));
    formik.current.setSubmitting(false);
    closeModal();
  };

  return (
    <YBModalForm
      dialogClassName={styles.modalDialog}
      visible={visible}
      showCancelButton
      submitLabel="Confirm"
      cancelLabel="Cancel"
      title="Manage Peer Certificates"
      onHide={closeModal}
      onFormSubmit={submitForm}
      initialValues={adaptPeerCertsToFormValues(peerCerts)}
      validate={validateForm}
      data-testid="ha-manage-peer-cert-modal"
      render={(formikProps: FormikProps<FormValues>) => {
        // workaround for outdated version of Formik to access form methods outside of <Formik>
        formik.current = formikProps;
        return (
          <>
            <div className={styles.formInstructionsContainer}>
              Submit peer certificates in PEM format.
            </div>
            <Form>
              <FieldArray
                name="peerCerts"
                render={(arrayHelpers) => (
                  <div>
                    {formik.current.values.peerCerts.map((peerCert, index) => (
                      // eslint-disable-next-line react/jsx-key
                      <div className={styles.certFormField}>
                        {formik.current.errors.peerCerts?.[index]?.data &&
                          formik.current.touched.peerCerts?.[index]?.data && (
                            <div className={styles.errorContainer}>
                              {formik.current.errors.peerCerts?.[index]?.data}
                            </div>
                          )}
                        <div className={styles.certItem}>
                          <Field
                            className={styles.certTextArea}
                            name={`peerCerts.${index}.data`}
                            component="textarea"
                          />
                          <YBButton
                            className={styles.removeCertButton}
                            btnIcon="fa fa-trash-o"
                            onClick={(e: any) => {
                              arrayHelpers.remove(index);
                              e.currentTarget.blur();
                            }}
                          />
                        </div>
                      </div>
                    ))}
                    <YBButton
                      className={styles.addCertButton}
                      btnText="Add Certificate"
                      btnIcon="fa fa-plus-circle"
                      onClick={(e: any) => {
                        arrayHelpers.push({ data: '' });
                        e.currentTarget.blur();
                      }}
                    />
                  </div>
                )}
              />
            </Form>
          </>
        );
      }}
    />
  );
};

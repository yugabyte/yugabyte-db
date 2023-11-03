import { FC, useEffect, useRef, useState } from 'react';
import { Field, FormikErrors, FormikProps } from 'formik';
import { useMutation, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';

import { YBModalForm } from '../../common/forms';
import { YBButton, YBFormInput } from '../../common/forms/fields';
import { api, QUERY_KEY } from '../../../redesign/helpers/api';
import { getPromiseState } from '../../../utils/PromiseUtils';
import {
  EMPTY_YB_HA_WEBSERVICE,
  getPeerCertIdentifier,
  getPeerCerts,
  YbHAWebService,
  YB_HA_WS_RUNTIME_CONFIG_KEY
} from '../replication/HAReplicationView';
import { ManagePeerCertsModal } from './ManagePeerCertsModal';
import { isCertCAEnabledInRuntimeConfig } from '../../customCACerts';

import styles from './AddStandbyInstanceModal.module.scss';

interface AddStandbyInstanceModalProps {
  visible: boolean;
  onClose(): void;
  configId: string;
  fetchRuntimeConfigs: () => void;
  setRuntimeConfig: (key: string, value: string) => void;
  runtimeConfigs: any;
}

interface AddStandbyInstanceFormValues {
  instanceAddress: string;
}

interface AddStandbyInstanceFormErrors {
  instanceAddress: string;
  peerCerts: string;
}

const INITIAL_VALUES: Partial<AddStandbyInstanceFormValues> = {
  instanceAddress: ''
};

const INSTANCE_VALID_ADDRESS_PATTERN = /^(http|https):\/\/.+/i;

export const AddStandbyInstanceModal: FC<AddStandbyInstanceModalProps> = ({
  visible,
  onClose,
  configId,
  fetchRuntimeConfigs,
  setRuntimeConfig,
  runtimeConfigs
}) => {
  const [isAddPeerCertsModalVisible, setAddPeerCertsModalVisible] = useState(false);

  const formik = useRef({} as FormikProps<AddStandbyInstanceFormValues>);
  const queryClient = useQueryClient();
  const { mutateAsync: createHAInstance } = useMutation((instanceAddress: string) =>
    api.createHAInstance(configId, instanceAddress, false, false)
  );

  useEffect(() => {
    fetchRuntimeConfigs();
  }, []); // eslint-disable-line react-hooks/exhaustive-deps

  const closeModal = () => {
    if (!formik.current.isSubmitting) onClose();
  };

  const submitForm = async (values: AddStandbyInstanceFormValues) => {
    try {
      await createHAInstance(values.instanceAddress);
      queryClient.invalidateQueries(QUERY_KEY.getHAConfig);
    } catch (error) {
      toast.error('Failed to add standby platform instance');
    } finally {
      formik.current.setSubmitting(false);
      closeModal();
    }
  };

  // fetch only specific key
  const showAddPeerCertModal = () => {
    fetchRuntimeConfigs();
    setAddPeerCertsModalVisible(true);
  };
  const hideAddPeerCertModal = () => {
    fetchRuntimeConfigs();
    setAddPeerCertsModalVisible(false);
  };
  const setYBHAWebserviceRuntimeConfig = (value: string) => {
    setRuntimeConfig(YB_HA_WS_RUNTIME_CONFIG_KEY, value);
  };

  const ybHAWebService: YbHAWebService =
    runtimeConfigs?.data && getPromiseState(runtimeConfigs).isSuccess()
      ? JSON.parse(
          runtimeConfigs.data.configEntries.find((c: any) => c.key === YB_HA_WS_RUNTIME_CONFIG_KEY)
            .value
        )
      : EMPTY_YB_HA_WEBSERVICE;
  const isCACertStoreEnabled = isCertCAEnabledInRuntimeConfig(runtimeConfigs?.data);
  const peerCerts = getPeerCerts(ybHAWebService);
  const isMissingPeerCerts = peerCerts.length === 0;

  if (visible) {
    return (
      <>
        <ManagePeerCertsModal
          visible={isAddPeerCertsModalVisible}
          peerCerts={peerCerts}
          setYBHAWebserviceRuntimeConfig={setYBHAWebserviceRuntimeConfig}
          onClose={hideAddPeerCertModal}
        />
        <YBModalForm
          visible
          initialValues={INITIAL_VALUES}
          validate={(values: AddStandbyInstanceFormValues) =>
            validateForm(values, isMissingPeerCerts, isCACertStoreEnabled)
          }
          validateOnChange
          validateOnBlur
          submitLabel="Continue"
          cancelLabel="Cancel"
          showCancelButton
          title="Add Standby Instance"
          onHide={closeModal}
          onFormSubmit={submitForm}
          render={(formikProps: FormikProps<AddStandbyInstanceFormValues>) => {
            // workaround for outdated version of Formik to access form methods outside of <Formik>
            formik.current = formikProps;
            const errors = formik.current.errors as FormikErrors<AddStandbyInstanceFormErrors>;

            const isHTTPS = formik.current.values?.instanceAddress?.startsWith('https:');
            return (
              <div data-testid="ha-add-standby-instance-modal">
                <Field
                  label="Enter IP Address / Hostname for standby platform instance"
                  name="instanceAddress"
                  placeholder="http://"
                  type="text"
                  component={YBFormInput}
                />
                {!isCACertStoreEnabled && isHTTPS && (
                  <div className={styles.peerCertsField}>
                    <div>
                      Please add one or more root CA cert needed to connect to each instance in the
                      HA cluster.
                    </div>
                    <div className={styles.certsContainer}>
                      {!isMissingPeerCerts && (
                        <>
                          <b>Peer Certificates:</b>
                          {peerCerts.map((peerCert) => {
                            return (
                              <div className={styles.certificate}>
                                <span className={styles.identifier}>
                                  {getPeerCertIdentifier(peerCert)}
                                </span>
                                <span className={styles.ellipse}>( . . . )</span>
                              </div>
                            );
                          })}
                        </>
                      )}
                    </div>
                    <YBButton
                      className={styles.addCertsButton}
                      btnText={
                        isMissingPeerCerts ? 'Add Peer Certificates' : 'Manage Peer Certificates'
                      }
                      btnIcon="fa fa-plus-circle"
                      onClick={(e: any) => {
                        e.preventDefault();
                        showAddPeerCertModal();
                      }}
                    />
                    {errors.peerCerts && (
                      <div className={styles.errorContainer}>{errors.peerCerts}</div>
                    )}
                  </div>
                )}
              </div>
            );
          }}
        />
      </>
    );
  } else {
    return null;
  }
};

const validateForm = (values: AddStandbyInstanceFormValues, isMissingPeerCerts: boolean, isCACertStoreEnabled: boolean) => {
  // Since our formik verision is < 2.0 , we need to throw errors instead of
  // returning them in custom async validation:
  // https://github.com/jaredpalmer/formik/issues/1392#issuecomment-606301031

  const errors: Partial<AddStandbyInstanceFormErrors> = {};
  if (!values.instanceAddress) {
    errors.instanceAddress = 'Required field';
  } else if (!INSTANCE_VALID_ADDRESS_PATTERN.test(values.instanceAddress)) {
    errors.instanceAddress = 'Must be a valid URL';
  } else if (!isCACertStoreEnabled && values.instanceAddress.startsWith('https:') && isMissingPeerCerts) {
    errors.peerCerts = 'A peer certificate is required for adding a standby instance over HTTPS';
  }

  return errors;
};

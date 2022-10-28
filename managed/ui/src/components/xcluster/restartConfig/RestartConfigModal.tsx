import React, { useRef, useState } from 'react';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { FormikActions, FormikProps } from 'formik';
import { toast } from 'react-toastify';

import { YBModalForm } from '../../common/forms';
import { PARALLEL_THREADS_RANGE } from '../../backupv2/common/BackupUtils';
import { YBButton, YBModal } from '../../common/forms/fields';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { ConfigureBootstrapStep } from './ConfigureBootstrapStep';
import { Universe } from '../../../redesign/helpers/dtos';
import { api } from '../../../redesign/helpers/api';
import { isYbcEnabledUniverse } from '../../../utils/UniverseUtils';
import {
  fetchTaskUntilItCompletes,
  restartXClusterConfig
} from '../../../actions/xClusterReplication';

import styles from './RestartConfigModal.module.scss';

export interface RestartXClusterConfigFormValues {
  storageConfig: { label: string; name: string; regions: any[]; value: string };
  parallelThreads: number;
}

export interface RestartXClusterConfigFormErrors {
  storageConfig: string;
  parallelThreads: string;
}

interface RestartConfigModalProps {
  onHide: () => void;
  isVisible: boolean;
  currentUniverseUUID: string;
  xClusterConfigUUID: string;
}

export enum FormStep {
  CONFIGURE_BOOTSTRAP = 'configureBootstrap'
}

const MODAL_TITLE = 'Restart Replication';
const FIRST_FORM_STEP = FormStep.CONFIGURE_BOOTSTRAP;
const INITIAL_VALUES: Partial<RestartXClusterConfigFormValues> = {
  parallelThreads: PARALLEL_THREADS_RANGE.MIN
};

export const RestartConfigModal = ({
  onHide,
  isVisible,
  currentUniverseUUID,
  xClusterConfigUUID
}: RestartConfigModalProps) => {
  const [currentStep, setCurrentStep] = useState<FormStep>(FIRST_FORM_STEP);
  const queryClient = useQueryClient();
  const formik = useRef({} as FormikProps<RestartXClusterConfigFormValues>);

  const universeQuery = useQuery<Universe>(['universe', currentUniverseUUID], () =>
    api.fetchUniverse(currentUniverseUUID)
  );

  const restartConfigMutation = useMutation(
    (values: RestartXClusterConfigFormValues) => {
      // Currently backend only supports restart replication for the
      // entire config. Table level restart support is coming soon.
      const tables: string[] = [];
      return restartXClusterConfig(xClusterConfigUUID, tables, {
        backupRequestParams: {
          storageConfigUUID: values.storageConfig.value,
          parallelism: values.parallelThreads,
          sse: values.storageConfig.name === 'S3',
          universeUUID: null
        }
      });
    },
    {
      onSuccess: (response) => {
        closeModal();

        fetchTaskUntilItCompletes(
          response.data.taskUUID,
          (err: boolean) => {
            if (err) {
              toast.error(
                <span className={styles.alertMsg}>
                  <i className="fa fa-exclamation-circle" />
                  <span>Replication restart failed.</span>
                  <a
                    href={`/tasks/${response.data.taskUUID}`}
                    rel="noopener noreferrer"
                    target="_blank"
                  >
                    View Details
                  </a>
                </span>
              );
            }
            queryClient.invalidateQueries(['Xcluster', xClusterConfigUUID]);
          },
          // Invalidate the cached data for current xCluster config. The xCluster config status should change to
          // 'in progress' once the restart config task starts.
          () => {
            queryClient.invalidateQueries(['Xcluster', xClusterConfigUUID]);
          }
        );
      },
      onError: (error: any) => {
        toast.error(
          <span className={styles.alertMsg}>
            <i className="fa fa-exclamation-circle" />
            <span>{error.message}</span>
          </span>
        );
      }
    }
  );

  const resetModalState = () => {
    setCurrentStep(FIRST_FORM_STEP);
  };
  const closeModal = () => {
    resetModalState();
    onHide();
  };

  const handleFormSubmit = async (
    values: RestartXClusterConfigFormValues,
    actions: FormikActions<RestartXClusterConfigFormValues>
  ) => {
    if (currentStep === FormStep.CONFIGURE_BOOTSTRAP) {
      restartConfigMutation.mutate(values);
    }
    actions.setSubmitting(false);
  };

  const submitLabel = getFormSubmitLabel(currentStep);

  if (universeQuery.isLoading || universeQuery.isIdle) {
    return (
      <YBModal
        size="large"
        title={MODAL_TITLE}
        visible={isVisible}
        onHide={() => {
          closeModal();
        }}
        submitLabel={submitLabel}
      >
        <YBLoading />
      </YBModal>
    );
  }

  if (universeQuery.isError) {
    return (
      <YBModal
        size="large"
        title={MODAL_TITLE}
        visible={isVisible}
        onHide={() => {
          closeModal();
        }}
      >
        <YBErrorIndicator />
      </YBModal>
    );
  }

  return (
    <YBModalForm
      size="large"
      title={MODAL_TITLE}
      visible={isVisible}
      validate={(values: RestartXClusterConfigFormValues) =>
        validateForm(values, currentStep, universeQuery.data)
      }
      onFormSubmit={handleFormSubmit}
      initialValues={INITIAL_VALUES}
      submitLabel={submitLabel}
      onHide={() => {
        closeModal();
      }}
      footerAccessory={<YBButton btnClass="btn" btnText={'Cancel'} onClick={closeModal} />}
      render={(formikProps: FormikProps<RestartXClusterConfigFormValues>) => {
        if (universeQuery.isLoading || universeQuery.isIdle) {
          return <YBLoading />;
        }

        if (universeQuery.isError) {
          return <YBErrorIndicator />;
        }

        // workaround for outdated version of Formik to access form methods outside of <Formik>
        formik.current = formikProps;
        switch (currentStep) {
          case FormStep.CONFIGURE_BOOTSTRAP:
            return <ConfigureBootstrapStep formik={formik} />;
        }
      }}
    />
  );
};

const validateForm = async (
  values: RestartXClusterConfigFormValues,
  formStep: FormStep,
  currentUniverse: Universe
) => {
  // Since our formik verision is < 2.0 , we need to throw errors instead of
  // returning them in custom async validation:
  // https://github.com/jaredpalmer/formik/issues/1392#issuecomment-606301031

  // We will add a `Select table` step shortly after the backend support lands.
  switch (formStep) {
    case FormStep.CONFIGURE_BOOTSTRAP: {
      const errors: Partial<RestartXClusterConfigFormErrors> = {};
      if (!values.storageConfig) {
        errors.storageConfig = 'Backup storage configuration is required.';
      }
      const shouldValidateParallelThread =
        values.parallelThreads && isYbcEnabledUniverse(currentUniverse?.universeDetails);
      if (shouldValidateParallelThread && values.parallelThreads > PARALLEL_THREADS_RANGE.MAX) {
        errors.parallelThreads = `Parallel threads must be less than or equal to ${PARALLEL_THREADS_RANGE.MAX}`;
      } else if (
        shouldValidateParallelThread &&
        values.parallelThreads < PARALLEL_THREADS_RANGE.MIN
      ) {
        errors.parallelThreads = `Parallel threads must be greater than or equal to ${PARALLEL_THREADS_RANGE.MIN}`;
      }

      throw errors;
    }
  }
};

const getFormSubmitLabel = (formStep: FormStep) => {
  switch (formStep) {
    case FormStep.CONFIGURE_BOOTSTRAP:
      return 'Restart Replication';
  }
};

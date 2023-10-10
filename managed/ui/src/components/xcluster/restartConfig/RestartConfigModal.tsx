import { useRef, useState } from 'react';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { FormikActions, FormikErrors, FormikProps } from 'formik';
import { toast } from 'react-toastify';
import { AxiosError } from 'axios';

import { YBModalForm } from '../../common/forms';
import { ParallelThreads } from '../../backupv2/common/BackupUtils';
import { YBButton, YBModal } from '../../common/forms/fields';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { ConfigureBootstrapStep } from './ConfigureBootstrapStep';
import { TableTypeLabel, Universe } from '../../../redesign/helpers/dtos';
import { api, universeQueryKey, xClusterQueryKey } from '../../../redesign/helpers/api';
import { isYbcEnabledUniverse } from '../../../utils/UniverseUtils';
import {
  fetchTaskUntilItCompletes,
  restartXClusterConfig
} from '../../../actions/xClusterReplication';
import { assertUnreachableCase, handleServerError } from '../../../utils/errorHandlingUtils';
import { ConfigTableSelect } from '../sharedComponents/tableSelect/ConfigTableSelect';
import { XClusterConfigStatus } from '../constants';

import { XClusterConfig, XClusterTableType } from '../XClusterTypes';

import styles from './RestartConfigModal.module.scss';

export interface RestartXClusterConfigFormValues {
  tableUUIDs: string[];
  // Bootstrap fields
  storageConfig: { label: string; name: string; regions: any[]; value: string };
  parallelThreads: number;
}

export interface RestartXClusterConfigFormErrors {
  tableUUIDs: { title: string; body: string };
  // Bootstrap fields
  storageConfig: string;
  parallelThreads: string;
}

export interface RestartXClusterConfigFormWarnings {
  tableUUIDs?: { title: string; body: string };
  // Bootstrap fields
  storageConfig?: string;
  parallelThreads?: string;
}

interface RestartConfigModalProps {
  configTableType: XClusterTableType;
  isVisible: boolean;
  onHide: () => void;
  xClusterConfig: XClusterConfig;
}

export const FormStep = {
  SELECT_TABLES: 'selectTables',
  CONFIGURE_BOOTSTRAP: 'configureBootstrap'
} as const;
// eslint-disable-next-line no-redeclare
export type FormStep = typeof FormStep[keyof typeof FormStep];

const MODAL_TITLE = 'Restart Replication';
const INITIAL_VALUES: Partial<RestartXClusterConfigFormValues> = {
  tableUUIDs: [],
  // Bootstrap fields
  parallelThreads: ParallelThreads.XCLUSTER_DEFAULT
};

export const RestartConfigModal = ({
  configTableType,
  isVisible,
  onHide,
  xClusterConfig
}: RestartConfigModalProps) => {
  // If xCluster config is in failed or initialized state, then we should restart the whole xCluster config.
  // Allowing partial restarts when the xCluster config is in intialized status is not expected behaviour.
  // Thus, we skip table selection for the xCluster config setup failed scenario.
  const firstFormStep =
    xClusterConfig.status === XClusterConfigStatus.FAILED ||
    xClusterConfig.status === XClusterConfigStatus.INITIALIZED
      ? FormStep.CONFIGURE_BOOTSTRAP
      : FormStep.SELECT_TABLES;
  const [currentStep, setCurrentStep] = useState<FormStep>(firstFormStep);
  const [formWarnings, setFormWarnings] = useState<RestartXClusterConfigFormWarnings>();

  // Need to store this to support navigating between pages
  const [selectedKeyspaces, setSelectedKeyspaces] = useState<string[]>([]);

  const queryClient = useQueryClient();
  const formik = useRef({} as FormikProps<RestartXClusterConfigFormValues>);

  const sourceUniverseQuery = useQuery<Universe>(
    universeQueryKey.detail(xClusterConfig.sourceUniverseUUID),
    () => api.fetchUniverse(xClusterConfig.sourceUniverseUUID)
  );

  const restartConfigMutation = useMutation(
    (values: RestartXClusterConfigFormValues) => {
      const tables: string[] = values.tableUUIDs;
      return restartXClusterConfig(xClusterConfig.uuid, tables, {
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
                  <span>{`Failed to restart replication: ${xClusterConfig.name}`}</span>
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
            queryClient.invalidateQueries(xClusterQueryKey.detail(xClusterConfig.uuid));
          },
          // Invalidate the cached data for current xCluster config. The xCluster config status should change to
          // 'in progress' once the restart config task starts.
          () => {
            queryClient.invalidateQueries(xClusterQueryKey.detail(xClusterConfig.uuid));
          }
        );
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, { customErrorLabel: 'Restart replication request failed' })
    }
  );

  const resetModalState = () => {
    setCurrentStep(firstFormStep);
    setFormWarnings({});
    setSelectedKeyspaces([]);
  };
  const closeModal = () => {
    resetModalState();
    onHide();
  };

  const handleFormSubmit = async (
    values: RestartXClusterConfigFormValues,
    actions: FormikActions<RestartXClusterConfigFormValues>
  ) => {
    switch (currentStep) {
      case FormStep.SELECT_TABLES:
        setCurrentStep(FormStep.CONFIGURE_BOOTSTRAP);
        actions.setSubmitting(false);
        return;
      case FormStep.CONFIGURE_BOOTSTRAP:
        restartConfigMutation.mutate(values, { onSettled: () => actions.setSubmitting(false) });
        return;
      default:
        assertUnreachableCase(currentStep);
    }
  };

  const submitLabel = getFormSubmitLabel(currentStep);
  if (sourceUniverseQuery.isLoading || sourceUniverseQuery.isIdle) {
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
  if (sourceUniverseQuery.isError) {
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
        validateForm(values, currentStep, sourceUniverseQuery.data)
      }
      onFormSubmit={handleFormSubmit}
      initialValues={INITIAL_VALUES}
      submitLabel={submitLabel}
      onHide={() => {
        closeModal();
      }}
      footerAccessory={<YBButton btnClass="btn" btnText={'Cancel'} onClick={closeModal} />}
      render={(formikProps: FormikProps<RestartXClusterConfigFormValues>) => {
        // workaround for outdated version of Formik to access form methods outside of <Formik>
        formik.current = formikProps;

        switch (currentStep) {
          case FormStep.SELECT_TABLES: {
            // Casting because FormikValues and FormikError have different types.
            const errors = formik.current.errors as FormikErrors<RestartXClusterConfigFormErrors>;
            return (
              <>
                <div className={styles.formInstruction}>
                  {`1. Select the ${TableTypeLabel[configTableType]} tables you want to restart replication for.`}
                </div>
                <ConfigTableSelect
                  {...{
                    xClusterConfig,
                    selectedTableUUIDs: formik.current.values.tableUUIDs,
                    setSelectedTableUUIDs: (tableUUIDs: string[]) =>
                      formik.current.setFieldValue('tableUUIDs', tableUUIDs),
                    configTableType,
                    selectedKeyspaces,
                    setSelectedKeyspaces,
                    selectionError: errors.tableUUIDs,
                    selectionWarning: formWarnings?.tableUUIDs
                  }}
                />
              </>
            );
          }
          case FormStep.CONFIGURE_BOOTSTRAP: {
            return (
              <>
                <div className={styles.formInstruction}>2. Configure bootstrap</div>
                <ConfigureBootstrapStep formik={formik} />
              </>
            );
          }
          default:
            return assertUnreachableCase(currentStep);
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

  switch (formStep) {
    case FormStep.SELECT_TABLES: {
      const errors: Partial<RestartXClusterConfigFormErrors> = {};
      if (!values.tableUUIDs || values.tableUUIDs.length === 0) {
        errors.tableUUIDs = {
          title: 'No tables selected.',
          body: 'Select at least 1 table to proceed'
        };
      }
      throw errors;
    }
    case FormStep.CONFIGURE_BOOTSTRAP: {
      const errors: Partial<RestartXClusterConfigFormErrors> = {};
      if (!values.storageConfig) {
        errors.storageConfig = 'Backup storage configuration is required.';
      }
      const shouldValidateParallelThread =
        values.parallelThreads && isYbcEnabledUniverse(currentUniverse?.universeDetails);
      if (shouldValidateParallelThread && values.parallelThreads > ParallelThreads.MAX) {
        errors.parallelThreads = `Parallel threads must be less than or equal to ${ParallelThreads.MAX}`;
      } else if (shouldValidateParallelThread && values.parallelThreads < ParallelThreads.MIN) {
        errors.parallelThreads = `Parallel threads must be greater than or equal to ${ParallelThreads.MIN}`;
      }

      throw errors;
    }
    default:
      return {};
  }
};

const getFormSubmitLabel = (formStep: FormStep) => {
  switch (formStep) {
    case FormStep.SELECT_TABLES:
      return 'Next: Configure Bootstrap';
    case FormStep.CONFIGURE_BOOTSTRAP:
      return 'Restart Replication';
    default:
      return assertUnreachableCase(formStep);
  }
};

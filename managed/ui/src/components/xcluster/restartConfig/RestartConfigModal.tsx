import { useRef, useState } from 'react';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { FormikActions, FormikErrors, FormikProps } from 'formik';
import { toast } from 'react-toastify';
import { AxiosError } from 'axios';
import { useTranslation } from 'react-i18next';

import { YBModalForm } from '../../common/forms';
import { YBButton, YBModal } from '../../common/forms/fields';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { ConfigureBootstrapStep } from './ConfigureBootstrapStep';
import { TableType, Universe, UniverseNamespace } from '../../../redesign/helpers/dtos';
import {
  api,
  drConfigQueryKey,
  universeQueryKey,
  xClusterQueryKey
} from '../../../redesign/helpers/api';
import {
  fetchTaskUntilItCompletes,
  restartXClusterConfig
} from '../../../actions/xClusterReplication';
import { assertUnreachableCase, handleServerError } from '../../../utils/errorHandlingUtils';
import { ConfigTableSelect } from '../sharedComponents/tableSelect/ConfigTableSelect';
import { XClusterConfigStatus } from '../constants';

import { XClusterTableType } from '../XClusterTypes';
import { XClusterConfig } from '../dtos';

import styles from './RestartConfigModal.module.scss';
import { DrConfig } from '../disasterRecovery/dtos';

export interface RestartXClusterConfigFormValues {
  tableUUIDs: string[];
  // Bootstrap fields
  storageConfig: { label: string; name: string; regions: any[]; value: string };
}

export interface RestartXClusterConfigFormErrors {
  tableUUIDs: { title: string; body: string };
  // Bootstrap fields
  storageConfig: string;
}

export interface RestartXClusterConfigFormWarnings {
  tableUUIDs?: { title: string; body: string };
  // Bootstrap fields
  storageConfig?: string;
}

interface CommonRestartConfigModalProps {
  configTableType: XClusterTableType;
  isVisible: boolean;
  onHide: () => void;
  xClusterConfig: XClusterConfig;
}
type RestartConfigModalProps =
  | (CommonRestartConfigModalProps & {
      isDrInterface: true;
      drConfig: DrConfig;
    })
  | (CommonRestartConfigModalProps & { isDrInterface: false });

export const FormStep = {
  SELECT_TABLES: 'selectTables',
  CONFIGURE_BOOTSTRAP: 'configureBootstrap'
} as const;
export type FormStep = typeof FormStep[keyof typeof FormStep];

const INITIAL_VALUES: Partial<RestartXClusterConfigFormValues> = {
  tableUUIDs: []
};

const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.restartReplicationModal';

export const RestartConfigModal = (props: RestartConfigModalProps) => {
  const { configTableType, isVisible, onHide, xClusterConfig } = props;
  // If xCluster config is in failed or initialized state, then we should restart the whole xCluster config.
  // Allowing partial restarts when the xCluster config is in intialized status is not expected behaviour.
  // Thus, we skip table selection for the xCluster config setup failed scenario.
  const isTableSelectionAllowed =
    xClusterConfig.status === XClusterConfigStatus.FAILED ||
    xClusterConfig.status === XClusterConfigStatus.INITIALIZED;
  const firstFormStep = isTableSelectionAllowed
    ? FormStep.CONFIGURE_BOOTSTRAP
    : FormStep.SELECT_TABLES;
  const [currentStep, setCurrentStep] = useState<FormStep>(firstFormStep);
  const [formWarnings, setFormWarnings] = useState<RestartXClusterConfigFormWarnings>();
  // Need to store this to support navigating between pages
  const [selectedKeyspaces, setSelectedKeyspaces] = useState<string[]>([]);

  const queryClient = useQueryClient();
  const formik = useRef({} as FormikProps<RestartXClusterConfigFormValues>);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const sourceUniverseQuery = useQuery<Universe>(
    universeQueryKey.detail(xClusterConfig.sourceUniverseUUID),
    () => api.fetchUniverse(xClusterConfig.sourceUniverseUUID)
  );
  const sourceUniverseNamespaceQuery = useQuery<UniverseNamespace[]>(
    universeQueryKey.namespaces(xClusterConfig.sourceUniverseUUID),
    () => api.fetchUniverseNamespaces(xClusterConfig.sourceUniverseUUID)
  );

  const namespaceToNamespaceUuid = Object.fromEntries(
    sourceUniverseNamespaceQuery.data?.map((namespace) => [
      namespace.name,
      namespace.namespaceUUID
    ]) ?? []
  );
  const restartConfigMutation = useMutation(
    (values: RestartXClusterConfigFormValues) => {
      return props.isDrInterface
        ? api.restartDrConfig(props.drConfig.uuid, {
            dbs: selectedKeyspaces.map((namespaceName) => namespaceToNamespaceUuid[namespaceName])
          })
        : restartXClusterConfig(xClusterConfig.uuid, values.tableUUIDs, {
            backupRequestParams: {
              storageConfigUUID: values.storageConfig.value
            }
          });
    },
    {
      onSuccess: (response) => {
        closeModal();

        const invalidateQueries = () => {
          if (props.isDrInterface) {
            queryClient.invalidateQueries(drConfigQueryKey.detail(props.drConfig.uuid));
          }
          queryClient.invalidateQueries(xClusterQueryKey.detail(xClusterConfig.uuid));
        };
        const handleTaskCompletion = (error: boolean) => {
          if (error) {
            toast.error(
              <span className={styles.alertMsg}>
                <i className="fa fa-exclamation-circle" />
                <span>{t('error.taskFailure')}</span>
                <a href={`/tasks/${response.taskUUID}`} rel="noopener noreferrer" target="_blank">
                  {t('viewDetails', { keyPrefix: 'task' })}
                </a>
              </span>
            );
          }
        };
        fetchTaskUntilItCompletes(response.taskUUID, handleTaskCompletion, invalidateQueries);
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, { customErrorLabel: t('error.requestFailureLabel') })
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

  const getFormSubmitLabel = (formStep: FormStep) => {
    switch (formStep) {
      case FormStep.SELECT_TABLES:
        return t(`step.selectTables.submitButton.${props.isDrInterface ? 'dr' : 'xCluster'}`);
      case FormStep.CONFIGURE_BOOTSTRAP:
        return t('step.configureBootstrap.submitButton');
      default:
        return assertUnreachableCase(formStep);
    }
  };
  const submitLabel = getFormSubmitLabel(currentStep);
  if (sourceUniverseQuery.isLoading || sourceUniverseQuery.isIdle) {
    return (
      <YBModal
        size="large"
        title={t('title')}
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
  if (sourceUniverseQuery.isError || sourceUniverseNamespaceQuery.isError) {
    return (
      <YBModal
        size="large"
        title={t('title')}
        visible={isVisible}
        onHide={() => {
          closeModal();
        }}
      >
        <YBErrorIndicator
          customErrorMessage={t('failedToFetchSourceUniverse', {
            keyPrefix: 'clusterDetail.xCluster.error'
          })}
        />
      </YBModal>
    );
  }

  return (
    <YBModalForm
      size="large"
      title={t(`title.${props.isDrInterface ? 'dr' : 'xCluster'}`)}
      visible={isVisible}
      validate={(values: RestartXClusterConfigFormValues) =>
        validateForm(values, currentStep, props.isDrInterface)
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
                  {t(
                    `step.selectTables.instruction.${
                      props.isDrInterface
                        ? 'dr'
                        : configTableType === TableType.PGSQL_TABLE_TYPE
                        ? 'xClusterYsql'
                        : 'xClusterYcql'
                    }`
                  )}
                </div>
                <ConfigTableSelect
                  {...{
                    xClusterConfig,
                    selectedTableUUIDs: formik.current.values.tableUUIDs,
                    setSelectedTableUUIDs: (tableUUIDs: string[]) =>
                      formik.current.setFieldValue('tableUUIDs', tableUUIDs),
                    isDrInterface: !!props.isDrInterface,
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
                <div className={styles.formInstruction}>
                  {t(
                    `step.configureBootstrap.instruction.${props.isDrInterface ? 'dr' : 'xCluster'}`
                  )}
                </div>
                <ConfigureBootstrapStep
                  isDrInterface={!!props.isDrInterface}
                  formik={formik}
                  storageConfigUuid={
                    props.isDrInterface
                      ? props.drConfig.bootstrapParams.backupRequestParams.storageConfigUUID
                      : undefined
                  }
                />
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
  isDrInterface: boolean
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
      if (!values.storageConfig && !isDrInterface) {
        errors.storageConfig = 'Backup storage configuration is required.';
      }

      throw errors;
    }
    default:
      return {};
  }
};

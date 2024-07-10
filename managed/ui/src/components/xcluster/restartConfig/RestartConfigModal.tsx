import { useRef, useState } from 'react';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { FormikActions, FormikErrors, FormikProps } from 'formik';
import { toast } from 'react-toastify';
import { AxiosError } from 'axios';
import { useTranslation } from 'react-i18next';
import { Typography } from '@material-ui/core';

import { YBModalForm } from '../../common/forms';
import { YBButton, YBModal } from '../../common/forms/fields';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { ConfigureBootstrapStep } from './ConfigureBootstrapStep';
import { ConfigTableSelect } from '../sharedComponents/tableSelect/ConfigTableSelect';
import {
  api,
  drConfigQueryKey,
  universeQueryKey,
  xClusterQueryKey
} from '../../../redesign/helpers/api';
import {
  fetchTablesInUniverse,
  fetchTaskUntilItCompletes,
  fetchXClusterConfig,
  restartXClusterConfig
} from '../../../actions/xClusterReplication';
import { isActionFrozen } from '../../../redesign/helpers/utils';
import { assertUnreachableCase, handleServerError } from '../../../utils/errorHandlingUtils';
import { XClusterConfig } from '../dtos';
import { AllowedTasks, TableType, Universe, YBTable } from '../../../redesign/helpers/dtos';
import { DrConfig } from '../disasterRecovery/dtos';
import {
  XClusterConfigStatus,
  XClusterTableStatus,
  XCLUSTER_UNIVERSE_TABLE_FILTERS
} from '../constants';
import { UNIVERSE_TASKS } from '../../../redesign/helpers/constants';
import { getXClusterConfigTableType } from '../ReplicationUtils';
import { getTableUuid } from '../../../utils/tableUtils';
import { XClusterTableType } from '../XClusterTypes';

import styles from './RestartConfigModal.module.scss';

export interface RestartXClusterConfigFormValues {
  tableUuids: string[];
  namespaceUuids: string[];
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
  isVisible: boolean;
  onHide: () => void;
  allowedTasks: AllowedTasks;
  xClusterConfigUuid: string;
}
type RestartConfigModalProps =
  | (CommonRestartConfigModalProps & {
      isDrInterface: true;
      drConfig: DrConfig;
    })
  | (CommonRestartConfigModalProps & { isDrInterface: false });

export const FormStep = {
  // Uninitialized step is used as a temporary step while
  // the xCluster config is still being fetched.
  // The actual first step of the form depends on the state of the
  // xCluster config.
  UNINITIALIZED: 'uninitialized',
  SELECT_TABLES: 'selectTables',
  CONFIGURE_BOOTSTRAP: 'configureBootstrap'
} as const;
export type FormStep = typeof FormStep[keyof typeof FormStep];

const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.restartReplicationModal';
const TRANSLATION_KEY_PREFIX_QUERY_ERROR = 'queryError';
const TRANSLATION_KEY_PREFIX_XCLUSTER = 'clusterDetail.xCluster';

export const RestartConfigModal = (props: RestartConfigModalProps) => {
  const { isVisible, onHide, xClusterConfigUuid } = props;

  const [currentFormStep, setCurrentFormStep] = useState<FormStep>(FormStep.UNINITIALIZED);
  const [formWarnings, setFormWarnings] = useState<RestartXClusterConfigFormWarnings>();

  const queryClient = useQueryClient();
  const formik = useRef({} as FormikProps<RestartXClusterConfigFormValues>);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  // We always want to fetch a fresh xCluster config before presenting the user with
  // xCluster table actions (add/remove/restart). This is because it gives the backend
  // an opportunity to sync with the DB and add/drop tables as needed.
  const xClusterConfigQuery = useQuery(
    xClusterQueryKey.detail(xClusterConfigUuid),
    () => fetchXClusterConfig(xClusterConfigUuid),
    { refetchOnMount: 'always' }
  );
  const sourceUniverseQuery = useQuery<Universe>(
    universeQueryKey.detail(xClusterConfigQuery.data?.sourceUniverseUUID),
    () => api.fetchUniverse(xClusterConfigQuery.data?.sourceUniverseUUID),
    { enabled: !!xClusterConfigQuery.data }
  );

  const sourceUniverseTablesQuery = useQuery<YBTable[]>(
    universeQueryKey.tables(
      xClusterConfigQuery.data?.sourceUniverseUUID,
      XCLUSTER_UNIVERSE_TABLE_FILTERS
    ),
    () =>
      fetchTablesInUniverse(
        xClusterConfigQuery.data?.sourceUniverseUUID,
        XCLUSTER_UNIVERSE_TABLE_FILTERS
      ).then((response) => response.data),
    { enabled: !!xClusterConfigQuery.data }
  );

  const restartConfigMutation = useMutation(
    (formValues: RestartXClusterConfigFormValues) => {
      return props.isDrInterface
        ? api.restartDrConfig(props.drConfig.uuid, {
            dbs: formValues.namespaceUuids
          })
        : restartXClusterConfig(xClusterConfigUuid, formValues.tableUuids, {
            backupRequestParams: {
              storageConfigUUID: formValues.storageConfig.value
            }
          });
    },
    {
      onSuccess: (response) => {
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
          } else {
            toast.success(
              <Typography variant="body2" component="span">
                {t(`success.taskSuccess.${props.isDrInterface ? 'dr' : 'xCluster'}`)}
              </Typography>
            );
          }
        };

        toast.success(
          <Typography variant="body2" component="span">
            {t('success.requestSuccess')}
          </Typography>
        );
        closeModal();
        fetchTaskUntilItCompletes(response.taskUUID, handleTaskCompletion, invalidateQueries);
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, { customErrorLabel: t('error.requestFailureLabel') })
    }
  );

  const resetModalState = () => {
    setCurrentFormStep(FormStep.UNINITIALIZED);
    setFormWarnings({});
  };
  const closeModal = () => {
    resetModalState();
    onHide();
  };

  const getFormSubmitLabel = (formStep: FormStep) => {
    switch (formStep) {
      case FormStep.UNINITIALIZED:
        return '';
      case FormStep.SELECT_TABLES:
        return t(`step.selectTables.submitButton.${props.isDrInterface ? 'dr' : 'xCluster'}`);
      case FormStep.CONFIGURE_BOOTSTRAP:
        return t('step.configureBootstrap.submitButton');
      default:
        return assertUnreachableCase(formStep);
    }
  };
  const modalTitle = t(`title.${props.isDrInterface ? 'dr' : 'xCluster'}`);
  if (
    xClusterConfigQuery.isLoading ||
    xClusterConfigQuery.isIdle ||
    sourceUniverseQuery.isLoading ||
    sourceUniverseQuery.isIdle ||
    sourceUniverseTablesQuery.isLoading ||
    sourceUniverseTablesQuery.isIdle
  ) {
    return (
      <YBModal
        size="large"
        title={modalTitle}
        visible={isVisible}
        onHide={() => {
          closeModal();
        }}
      >
        <YBLoading />
      </YBModal>
    );
  }

  if (xClusterConfigQuery.isError) {
    return (
      <YBModal
        title={modalTitle}
        visible={isVisible}
        onHide={() => {
          closeModal();
        }}
      >
        <YBErrorIndicator
          customErrorMessage={t('failedToFetchXClusterConfig', {
            keyPrefix: TRANSLATION_KEY_PREFIX_QUERY_ERROR,
            xClusterConfigUuid: xClusterConfigUuid
          })}
        />
      </YBModal>
    );
  }

  const xClusterConfig = xClusterConfigQuery.data;
  const configTableType = getXClusterConfigTableType(
    xClusterConfig,
    sourceUniverseTablesQuery.data
  );
  if (
    sourceUniverseQuery.isError ||
    sourceUniverseTablesQuery.isError ||
    configTableType === null
  ) {
    const errorMessage = sourceUniverseQuery.isError
      ? props.isDrInterface
        ? t('failedToFetchDrPrimaryUniverse', {
            keyPrefix: TRANSLATION_KEY_PREFIX_QUERY_ERROR,
            universeUuid: xClusterConfig.sourceUniverseUUID
          })
        : t('failedToFetchSourceUniverse', {
            keyPrefix: TRANSLATION_KEY_PREFIX_QUERY_ERROR,
            universeUuid: xClusterConfig.sourceUniverseUUID
          })
      : sourceUniverseTablesQuery.isError
      ? props.isDrInterface
        ? t('failedToFetchDrPrimaryTables', {
            keyPrefix: TRANSLATION_KEY_PREFIX_QUERY_ERROR,
            universeUuid: xClusterConfig.sourceUniverseUUID
          })
        : t('failedToFetchSourceUniverseTables', {
            keyPrefix: TRANSLATION_KEY_PREFIX_QUERY_ERROR,
            universeUuid: xClusterConfig.sourceUniverseUUID
          })
      : t('error.undefinedXClusterTableType', {
          keyPrefix: TRANSLATION_KEY_PREFIX_XCLUSTER
        });
    return (
      <YBModal
        size="large"
        title={modalTitle}
        visible={isVisible}
        onHide={() => {
          closeModal();
        }}
      >
        <YBErrorIndicator customErrorMessage={errorMessage} />
      </YBModal>
    );
  }
  // If xCluster config is in failed or initialized state, then we should restart the whole xCluster config.
  // Allowing partial restarts when the xCluster config is in initialized status is not expected behavior.
  // Thus, we skip table selection for the xCluster config setup failed scenario.
  const isTableSelectionAllowed = !(
    xClusterConfig.status === XClusterConfigStatus.FAILED ||
    xClusterConfig.status === XClusterConfigStatus.INITIALIZED
  );
  const firstFormStep = !isTableSelectionAllowed
    ? FormStep.CONFIGURE_BOOTSTRAP
    : FormStep.SELECT_TABLES;
  if (currentFormStep === FormStep.UNINITIALIZED) {
    setCurrentFormStep(firstFormStep);
  }

  const handleFormSubmit = async (
    values: RestartXClusterConfigFormValues,
    actions: FormikActions<RestartXClusterConfigFormValues>
  ) => {
    switch (currentFormStep) {
      case FormStep.UNINITIALIZED:
        return;
      case FormStep.SELECT_TABLES:
        setCurrentFormStep(FormStep.CONFIGURE_BOOTSTRAP);
        actions.setSubmitting(false);
        return;
      case FormStep.CONFIGURE_BOOTSTRAP:
        restartConfigMutation.mutate(values, { onSettled: () => actions.setSubmitting(false) });
        return;
      default:
        assertUnreachableCase(currentFormStep);
    }
  };

  const { defaultTableUuids, defaultNamespaces } = getDefaultFormValues(
    xClusterConfig,
    sourceUniverseTablesQuery.data,
    configTableType
  );
  const initialValues: Partial<RestartXClusterConfigFormValues> = {
    // Preselect all the tables in `ERROR` status, because in most cases these are the
    // tables that the user wants to restart.
    tableUuids: defaultTableUuids,
    namespaceUuids: defaultNamespaces
  };

  const isButtonDisabled = props.isDrInterface
    ? isActionFrozen(props.allowedTasks, UNIVERSE_TASKS.RESTART_DR)
    : isActionFrozen(props.allowedTasks, UNIVERSE_TASKS.RESTART_REPLICATION);
  const submitLabel = getFormSubmitLabel(currentFormStep);
  return (
    <YBModalForm
      size="large"
      title={modalTitle}
      visible={isVisible}
      validate={(values: RestartXClusterConfigFormValues) =>
        validateForm(values, currentFormStep, props.isDrInterface, configTableType)
      }
      onFormSubmit={handleFormSubmit}
      initialValues={initialValues}
      submitLabel={submitLabel}
      onHide={() => {
        closeModal();
      }}
      isButtonDisabled={isButtonDisabled}
      footerAccessory={<YBButton btnClass="btn" btnText={'Cancel'} onClick={closeModal} />}
      render={(formikProps: FormikProps<RestartXClusterConfigFormValues>) => {
        // workaround for outdated version of Formik to access form methods outside of <Formik>
        formik.current = formikProps;

        switch (currentFormStep) {
          case FormStep.UNINITIALIZED:
            // This is unreachable because we initialize the form step after fetching the xCluster config.
            // We also display an error or loading page before reaching this return if the xCluster config
            // query is not successfully completed.
            return <></>;
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
                    selectedTableUuids: formik.current.values.tableUuids,
                    setSelectedTableUuids: (tableUuids: string[]) =>
                      formik.current.setFieldValue('tableUuids', tableUuids),
                    isDrInterface: !!props.isDrInterface,
                    configTableType,
                    selectedNamespaceUuids: formik.current.values.namespaceUuids,
                    setSelectedNamespaceUuids: (namespaceUuids: string[]) =>
                      formik.current.setFieldValue('namespaceUuids', namespaceUuids),
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
            return assertUnreachableCase(currentFormStep);
        }
      }}
    />
  );
};

const validateForm = async (
  values: RestartXClusterConfigFormValues,
  formStep: FormStep,
  isDrInterface: boolean,
  tableType: XClusterTableType
) => {
  // Since our formik version is < 2.0 , we need to throw errors instead of
  // returning them in custom async validation:
  // https://github.com/jaredpalmer/formik/issues/1392#issuecomment-606301031

  switch (formStep) {
    case FormStep.SELECT_TABLES: {
      const errors: Partial<RestartXClusterConfigFormErrors> = {};
      if (!values.tableUuids || values.tableUuids.length === 0) {
        errors.tableUUIDs = {
          title: `No ${
            tableType === TableType.PGSQL_TABLE_TYPE ? 'databases' : 'tables'
          } selected.`,
          body: `Select at least 1 ${
            tableType === TableType.PGSQL_TABLE_TYPE ? 'database' : 'table'
          } to proceed`
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

const getDefaultFormValues = (
  xClusterConfig: XClusterConfig,
  sourceUniverseTables: YBTable[],
  configTableType: XClusterTableType
): { defaultTableUuids: string[]; defaultNamespaces: string[] } => {
  const tableUuidsInErrorStatus = xClusterConfig.tableDetails
    .filter((tableDetail) => tableDetail.status === XClusterTableStatus.ERROR)
    .map((tableDetail) => tableDetail.tableId);
  if (configTableType === TableType.YQL_TABLE_TYPE) {
    return {
      defaultTableUuids: tableUuidsInErrorStatus,
      defaultNamespaces: []
    };
  }

  // For YSQL, backup and restore can only be done at the database level.
  // Thus, we preselect all tables in any database containing tables in error state.
  const selectedTableUuids = new Set<string>();
  const selectedNamespace = new Set<string>();
  const ysqlNamespaceToTableUuids = new Map<string, Set<string>>();

  sourceUniverseTables.forEach((table) => {
    const tableUUIDs = ysqlNamespaceToTableUuids.get(table.keySpace);
    if (tableUUIDs !== undefined) {
      tableUUIDs.add(getTableUuid(table));
    } else {
      ysqlNamespaceToTableUuids.set(
        table.keySpace,
        new Set<string>([getTableUuid(table)])
      );
    }
    if (tableUuidsInErrorStatus.includes(getTableUuid(table))) {
      selectedNamespace.add(table.keySpace);
    }
  });
  selectedNamespace.forEach((namespace) => {
    const tableUuids = ysqlNamespaceToTableUuids.get(namespace);
    tableUuids?.forEach((tableUuid) => {
      if (xClusterConfig.tables.includes(tableUuid)) {
        selectedTableUuids.add(tableUuid);
      }
    });
  });
  return {
    defaultTableUuids: Array.from(selectedTableUuids),
    defaultNamespaces: Array.from(selectedNamespace)
  };
};

import { useState } from 'react';
import { AxiosError } from 'axios';
import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { toast } from 'react-toastify';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { useTranslation } from 'react-i18next';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';

import {
  createXClusterConfig,
  CreateXClusterConfigRequest,
  fetchTablesInUniverse,
  fetchTaskUntilItCompletes,
  fetchUniverseDiskUsageMetric
} from '../../../actions/xClusterReplication';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { getTablesForBootstrapping, parseFloatIfDefined } from '../ReplicationUtils';
import { assertUnreachableCase, handleServerError } from '../../../utils/errorHandlingUtils';
import {
  api,
  drConfigQueryKey,
  runtimeConfigQueryKey,
  universeQueryKey
} from '../../../redesign/helpers/api';
import { YBButton, YBModal, YBModalProps } from '../../../redesign/components';
import { StorageConfigOption } from '../sharedComponents/ReactSelectStorageConfig';
import { CurrentFormStep } from './CurrentFormStep';
import {
  BOOTSTRAP_MIN_FREE_DISK_SPACE_GB,
  XClusterConfigAction,
  XClusterConfigType,
  XCLUSTER_UNIVERSE_TABLE_FILTERS
} from '../constants';
import { RuntimeConfigKey } from '../../../redesign/helpers/constants';

import { XClusterTableType } from '../XClusterTypes';
import {
  TableType,
  TableTypeLabel,
  Universe,
  UniverseNamespace,
  YBTable
} from '../../../redesign/helpers/dtos';

import toastStyles from '../../../redesign/styles/toastStyles.module.scss';

interface CreateConfigModalProps {
  modalProps: YBModalProps;
  sourceUniverseUuid: string;
}

export interface CreateXClusterConfigFormValues {
  configName: string;
  targetUniverse: { label: string; value: Universe };
  tableType: { label: string; value: XClusterTableType };
  isTransactionalConfig: boolean;
  namespaceUuids: string[];
  tableUuids: string[];
  storageConfig: StorageConfigOption;
}

export interface CreateXClusterConfigFormErrors {
  configName: string;
  targetUniverse: string;
  namespaceUuids: { title: string; body: string };
  storageConfig: string;
}

export interface CreateXClusterConfigFormWarnings {
  configName?: string;
  targetUniverse?: string;
  namespaceUuids?: { title: string; body: string };
  storageConfig?: string;
}

export const FormStep = {
  SELECT_TARGET_UNIVERSE: 'selectTargetUniverse',
  SELECT_TABLES: 'selectDatabases',
  CONFIGURE_BOOTSTRAP: 'configureBootstrap',
  CONFIRM_ALERT: 'configureAlert'
} as const;
export type FormStep = typeof FormStep[keyof typeof FormStep];

const useStyles = makeStyles(() => ({
  secondarySubmitButton: {
    marginLeft: 'auto'
  }
}));

const MODAL_NAME = 'CreateConfigModal';
const FIRST_FORM_STEP = FormStep.SELECT_TARGET_UNIVERSE;
const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.createConfigModal';
const TRANSLATION_KEY_PREFIX_SELECT_TABLE = 'clusterDetail.xCluster.selectTable';

export const CreateConfigModal = ({ modalProps, sourceUniverseUuid }: CreateConfigModalProps) => {
  const [currentFormStep, setCurrentFormStep] = useState<FormStep>(FIRST_FORM_STEP);
  const [tableSelectionError, setTableSelectionError] = useState<{
    title: string;
    body: string;
  } | null>(null);
  const [tableSelectionWarning, setTableSelectionWarning] = useState<{
    title: string;
    body: string;
  } | null>(null);
  const [bootstrapRequiredTableUUIDs, setBootstrapRequiredTableUUIDs] = useState<string[]>([]);
  const [isTableSelectionValidated, setIsTableSelectionValidated] = useState<boolean>(false);
  const [skipBootstrapping, setSkipBootStrapping] = useState<boolean>(false);
  // The purpose of committedTargetUniverse is to store the targetUniverse field value prior
  // to the user submitting their target universe step.
  // This value updates whenever the user submits SelectTargetUniverseStep with a new
  // target universe.
  const [committedTargetUniverseUuid, setCommittedTargetUniverseUuid] = useState<string>();
  const [committedTableType, setCommittedTableType] = useState<XClusterTableType>();

  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const queryClient = useQueryClient();
  const theme = useTheme();
  const classes = useStyles();

  const xClusterConfigMutation = useMutation(
    (formValues: CreateXClusterConfigFormValues) => {
      const createXClusterConfigRequest: CreateXClusterConfigRequest = {
        name: formValues.configName,
        sourceUniverseUUID: sourceUniverseUuid,
        targetUniverseUUID: formValues.targetUniverse.value.universeUUID,
        configType: formValues.isTransactionalConfig
          ? XClusterConfigType.TXN
          : XClusterConfigType.BASIC,
        tables: formValues.tableUuids,

        ...(!skipBootstrapping &&
          bootstrapRequiredTableUUIDs.length > 0 && {
            bootstrapParams: {
              tables: bootstrapRequiredTableUUIDs,
              allowBootstrapping: true,

              backupRequestParams: {
                storageConfigUUID: formValues.storageConfig.value.uuid
              }
            }
          })
      };
      return createXClusterConfig(createXClusterConfigRequest);
    },
    {
      onSuccess: async (response, values) => {
        const invalidateQueries = () => {
          queryClient.invalidateQueries(drConfigQueryKey.detail(response.resourceUUID));
          // The new DR config will update the sourceXClusterConfigs for the source universe and
          // to targetXClusterConfigs for the target universe.
          // Invalidate queries for the participating universes.
          queryClient.invalidateQueries(universeQueryKey.detail(sourceUniverseUuid), {
            exact: true
          });
          queryClient.invalidateQueries(
            universeQueryKey.detail(values.targetUniverse.value.universeUUID),
            { exact: true }
          );

          queryClient.invalidateQueries(drConfigQueryKey.detail(response.resourceUUID));
        };
        const handleTaskCompletion = (error: boolean) => {
          if (error) {
            toast.error(
              <span className={toastStyles.toastMessage}>
                <i className="fa fa-exclamation-circle" />
                <Typography variant="body2" component="span">
                  {t('error.taskFailure')}
                </Typography>
                <a href={`/tasks/${response.taskUUID}`} rel="noopener noreferrer" target="_blank">
                  {t('viewDetails', { keyPrefix: 'task' })}
                </a>
              </span>
            );
          } else {
            toast.success(
              <Typography variant="body2" component="span">
                {t('success.taskSuccess')}
              </Typography>
            );
          }
        };

        toast.success(
          <Typography variant="body2" component="span">
            {t('success.requestSuccess')}
          </Typography>
        );
        modalProps.onClose();
        fetchTaskUntilItCompletes(response.taskUUID, handleTaskCompletion, invalidateQueries);
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, { customErrorLabel: t('error.requestFailureLabel') })
    }
  );

  const sourceUniverseTablesQuery = useQuery<YBTable[]>(
    universeQueryKey.tables(sourceUniverseUuid, XCLUSTER_UNIVERSE_TABLE_FILTERS),
    () =>
      fetchTablesInUniverse(sourceUniverseUuid, XCLUSTER_UNIVERSE_TABLE_FILTERS).then(
        (response) => response.data
      )
  );
  const sourceUniverseQuery = useQuery<Universe>(universeQueryKey.detail(sourceUniverseUuid), () =>
    api.fetchUniverse(sourceUniverseUuid)
  );

  const customerUuid = localStorage.getItem('customerId') ?? '';
  const runtimeConfigQuery = useQuery(runtimeConfigQueryKey.customerScope(customerUuid), () =>
    api.fetchRuntimeConfigs(sourceUniverseUuid, true)
  );

  const formMethods = useForm<CreateXClusterConfigFormValues>({
    defaultValues: {
      namespaceUuids: [],
      tableUuids: [],
      tableType: {
        label: TableTypeLabel[TableType.PGSQL_TABLE_TYPE],
        value: TableType.PGSQL_TABLE_TYPE
      },
      isTransactionalConfig: true
    }
  });

  const modalTitle = t('title');
  if (
    sourceUniverseTablesQuery.isLoading ||
    sourceUniverseTablesQuery.isIdle ||
    sourceUniverseQuery.isLoading ||
    sourceUniverseQuery.isIdle ||
    runtimeConfigQuery.isLoading ||
    runtimeConfigQuery.isIdle
  ) {
    return (
      <YBModal
        title={modalTitle}
        submitTestId={`${MODAL_NAME}-SubmitButton`}
        maxWidth="xl"
        size="md"
        overrideWidth="960px"
        {...modalProps}
      >
        <YBLoading />
      </YBModal>
    );
  }

  if (
    sourceUniverseTablesQuery.isError ||
    sourceUniverseQuery.isError ||
    runtimeConfigQuery.isError
  ) {
    const errorMessage = runtimeConfigQuery.isError
      ? t('failedToFetchCustomerRuntimeConfig', { keyPrefix: 'queryError' })
      : t('failedToFetchSourceUniverse', {
          keyPrefix: 'clusterDetail.xCluster.error',
          universeUuid: sourceUniverseUuid
        });
    return (
      <YBModal
        title={modalTitle}
        submitTestId={`${MODAL_NAME}-SubmitButton`}
        maxWidth="xl"
        size="md"
        overrideWidth="960px"
        {...modalProps}
      >
        <YBErrorIndicator customErrorMessage={errorMessage} />
      </YBModal>
    );
  }

  /**
   * Clear any existing table selection errors/warnings
   * The new table/namespace selection will need to be (re)validated.
   */
  const clearTableSelectionFeedback = () => {
    setTableSelectionError(null);
    setTableSelectionWarning(null);
    setIsTableSelectionValidated(false);
  };
  const setSelectedNamespaceUuids = (namespaces: string[]) => {
    clearTableSelectionFeedback();
    formMethods.clearErrors('namespaceUuids');

    // We will run any required validation on selected namespaces & tables all at once when the
    // user clicks on the 'Validate Selection' button.
    formMethods.setValue('namespaceUuids', namespaces, { shouldValidate: false });
  };
  const setSelectedTableUuids = (tableUuids: string[]) => {
    // Clear any existing errors.
    // The new table/namespace selection will need to be (re)validated.
    clearTableSelectionFeedback();
    formMethods.clearErrors('tableUuids');

    // We will run any required validation on selected namespaces & tables all at once when the
    // user clicks on the 'Validate Selection' button.
    formMethods.setValue('tableUuids', tableUuids, { shouldValidate: false });
  };

  /**
   * Clear table/namespace selection.
   */
  const resetTableSelection = () => {
    setSelectedTableUuids([]);
    setSelectedNamespaceUuids([]);
    setIsTableSelectionValidated(false);
  };

  const sourceUniverseTables = sourceUniverseTablesQuery.data;

  const onSubmit = async (
    formValues: CreateXClusterConfigFormValues,
    skipBootstrapping: boolean
  ) => {
    // When the user changes target universe or table type, the old table selection is no longer valid.
    const isTableSelectionInvalidated =
      formValues.targetUniverse.value.universeUUID !== committedTargetUniverseUuid ||
      formValues.tableType.value !== committedTableType;
    switch (currentFormStep) {
      case FormStep.SELECT_TARGET_UNIVERSE:
        if (isTableSelectionInvalidated) {
          resetTableSelection();
        }
        if (formValues.targetUniverse.value.universeUUID !== committedTargetUniverseUuid) {
          setCommittedTargetUniverseUuid(formValues.targetUniverse.value.universeUUID);
        }
        if (formValues.tableType.value !== committedTableType) {
          setCommittedTableType(formValues.tableType.value);
        }
        setCurrentFormStep(FormStep.SELECT_TABLES);
        return;
      case FormStep.SELECT_TABLES:
        setTableSelectionError(null);
        if (formValues.tableUuids.length <= 0) {
          formMethods.setError('tableUuids', {
            type: 'min',
            message: t('error.validationMinimumNamespaceUuids.title', {
              keyPrefix: TRANSLATION_KEY_PREFIX_SELECT_TABLE
            })
          });
          // The TableSelect component expects error objects with title and body fields.
          // React-hook-form only allows string error messages.
          // Thus, we need an store these error objects separately.
          setTableSelectionError({
            title: t('error.validationMinimumNamespaceUuids.title', {
              keyPrefix: TRANSLATION_KEY_PREFIX_SELECT_TABLE
            }),
            body: t('error.validationMinimumNamespaceUuids.body', {
              keyPrefix: TRANSLATION_KEY_PREFIX_SELECT_TABLE
            })
          });
          return;
        }

        setSkipBootStrapping(skipBootstrapping);
        if (skipBootstrapping) {
          setCurrentFormStep(FormStep.CONFIRM_ALERT);
          return;
        }

        if (!isTableSelectionValidated) {
          let bootstrapTableUuids: string[] | null = null;
          const hasSelectionError = false;

          if (formValues.tableUuids.length) {
            try {
              bootstrapTableUuids = await getTablesForBootstrapping(
                formValues.tableUuids,
                sourceUniverseUuid,
                targetUniverseUuid,
                sourceUniverseTables,
                formValues.isTransactionalConfig ? XClusterConfigType.TXN : XClusterConfigType.BASIC
              );
            } catch (error: any) {
              toast.error(
                <Box display="flex" flexDirection="column" gridGap={theme.spacing(1)}>
                  <div className={toastStyles.toastMessage}>
                    <i className="fa fa-exclamation-circle" />
                    <Typography variant="body2" component="span">
                      {t('error.failedToFetchIsBootstrapRequired.title', {
                        keyPrefix: TRANSLATION_KEY_PREFIX_SELECT_TABLE
                      })}
                    </Typography>
                  </div>
                  <Typography variant="body2" component="div">
                    {t('error.failedToFetchIsBootstrapRequired.body', {
                      keyPrefix: TRANSLATION_KEY_PREFIX_SELECT_TABLE
                    })}
                  </Typography>
                  <Typography variant="body2" component="div">
                    {error.message}
                  </Typography>
                </Box>
              );
              setTableSelectionWarning({
                title: t('error.failedToFetchIsBootstrapRequired.title', {
                  keyPrefix: TRANSLATION_KEY_PREFIX_SELECT_TABLE
                }),
                body: t('error.failedToFetchIsBootstrapRequired.body', {
                  keyPrefix: TRANSLATION_KEY_PREFIX_SELECT_TABLE
                })
              });
            }
          }

          if (bootstrapTableUuids?.length && bootstrapTableUuids?.length > 0) {
            setBootstrapRequiredTableUUIDs(bootstrapTableUuids);

            // Validate that the source universe has at least the recommended amount of
            // disk space if bootstrapping is required.
            const currentUniverseNodePrefix = sourceUniverse.universeDetails.nodePrefix;
            const diskUsageMetric = await fetchUniverseDiskUsageMetric(currentUniverseNodePrefix);
            const freeSpaceTrace = diskUsageMetric.disk_usage.data.find(
              (trace) => trace.name === 'free'
            );
            const freeDiskSpace = parseFloatIfDefined(
              freeSpaceTrace?.y[freeSpaceTrace.y.length - 1]
            );

            if (freeDiskSpace !== undefined && freeDiskSpace < BOOTSTRAP_MIN_FREE_DISK_SPACE_GB) {
              setTableSelectionWarning({
                title: t('warning.insufficientDiskSpace.title', {
                  keyPrefix: TRANSLATION_KEY_PREFIX_SELECT_TABLE
                }),
                body: t('warning.insufficientDiskSpace.body', {
                  keyPrefix: TRANSLATION_KEY_PREFIX_SELECT_TABLE,
                  bootstrapMinFreeDiskSpaceGb: BOOTSTRAP_MIN_FREE_DISK_SPACE_GB
                })
              });
            }
          }
          if (hasSelectionError === false) {
            setIsTableSelectionValidated(true);
          }
          return;
        }

        if (bootstrapRequiredTableUUIDs.length > 0) {
          setCurrentFormStep(FormStep.CONFIGURE_BOOTSTRAP);
        } else {
          setCurrentFormStep(FormStep.CONFIRM_ALERT);
        }
        return;
      case FormStep.CONFIGURE_BOOTSTRAP:
        setCurrentFormStep(FormStep.CONFIRM_ALERT);
        return;
      case FormStep.CONFIRM_ALERT: {
        return xClusterConfigMutation.mutateAsync(formValues);
      }
      default:
        return assertUnreachableCase(currentFormStep);
    }
  };
  const onFormSubmit: SubmitHandler<CreateXClusterConfigFormValues> = async (formValues) =>
    onSubmit(formValues, false);
  const onSkipBootstrapAndSubmit: SubmitHandler<CreateXClusterConfigFormValues> = async (
    formValues
  ) => onSubmit(formValues, true);
  const handleBackNavigation = () => {
    // We can clear errors here because prior steps have already been validated
    // and future steps will be revalidated when the user clicks the next page button.
    formMethods.clearErrors();

    switch (currentFormStep) {
      case FIRST_FORM_STEP:
        return;
      case FormStep.SELECT_TABLES:
        setCurrentFormStep(FormStep.SELECT_TARGET_UNIVERSE);
        return;
      case FormStep.CONFIGURE_BOOTSTRAP:
        setCurrentFormStep(FormStep.SELECT_TABLES);
        return;
      case FormStep.CONFIRM_ALERT:
        if (bootstrapRequiredTableUUIDs.length > 0 && !skipBootstrapping) {
          setCurrentFormStep(FormStep.CONFIGURE_BOOTSTRAP);
        } else {
          setCurrentFormStep(FormStep.SELECT_TABLES);
        }
        return;
      default:
        assertUnreachableCase(currentFormStep);
    }
  };

  const tableType = formMethods.watch('tableType')?.value;
  const getFormSubmitLabel = (
    formStep: FormStep,
    isTableSelectionValidated: boolean,
    isBootstrapStepRequired: boolean
  ) => {
    switch (formStep) {
      case FormStep.SELECT_TARGET_UNIVERSE:
        return t(
          `step.selectTargetUniverse.submitButton.${
            tableType === TableType.PGSQL_TABLE_TYPE ? 'selectDatabases' : 'selectTables'
          }`
        );
      case FormStep.SELECT_TABLES:
        if (!isTableSelectionValidated) {
          return t('step.selectTables.submitButton.validateSelection');
        }
        if (isBootstrapStepRequired) {
          return t('step.selectTables.submitButton.configureBootstrap');
        }
        return t('step.selectTables.submitButton.confirmAlert');
      case FormStep.CONFIGURE_BOOTSTRAP:
        return t('step.configureBootstrap.submitButton');
      case FormStep.CONFIRM_ALERT:
        return t('confirmAlert.submitButton', { keyPrefix: 'clusterDetail.xCluster.shared' });
      default:
        return assertUnreachableCase(formStep);
    }
  };

  const isBootstrapStepRequired = bootstrapRequiredTableUUIDs.length > 0;
  const sourceUniverse = sourceUniverseQuery.data;
  const submitLabel = getFormSubmitLabel(
    currentFormStep,
    isTableSelectionValidated,
    isBootstrapStepRequired
  );
  const selectedTableUuids = formMethods.watch('tableUuids');
  const selectedNamespaceUuids = formMethods.watch('namespaceUuids');
  const targetUniverseUuid = formMethods.watch('targetUniverse.value.universeUUID');
  const isTransactionalConfig = formMethods.watch('isTransactionalConfig');

  const isFormDisabled = formMethods.formState.isSubmitting;
  const runtimeConfigEntries = runtimeConfigQuery.data.configEntries ?? [];
  const isSkipBootstrappingEnabled = runtimeConfigEntries.some(
    (config: any) =>
      config.key === RuntimeConfigKey.ENABLE_XCLUSTER_SKIP_BOOTSTRAPPING && config.value === 'true'
  );
  return (
    <YBModal
      title={modalTitle}
      submitLabel={submitLabel}
      buttonProps={{ primary: { disabled: isFormDisabled } }}
      onSubmit={formMethods.handleSubmit(onFormSubmit)}
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      isSubmitting={formMethods.formState.isSubmitting}
      showSubmitSpinner={currentFormStep !== FormStep.SELECT_TABLES || !skipBootstrapping}
      maxWidth="xl"
      size={
        ([FormStep.SELECT_TARGET_UNIVERSE, FormStep.SELECT_TABLES] as FormStep[]).includes(
          currentFormStep
        )
          ? 'fit'
          : 'md'
      }
      overrideWidth="960px"
      footerAccessory={
        <>
          {currentFormStep !== FIRST_FORM_STEP && (
            <YBButton variant="secondary" onClick={handleBackNavigation}>
              {t('back', { keyPrefix: 'common' })}
            </YBButton>
          )}
          {currentFormStep === FormStep.SELECT_TABLES &&
            isBootstrapStepRequired &&
            isSkipBootstrappingEnabled && (
              <YBButton
                className={classes.secondarySubmitButton}
                variant="secondary"
                onClick={formMethods.handleSubmit(onSkipBootstrapAndSubmit)}
                showSpinner={formMethods.formState.isSubmitting && skipBootstrapping}
                disabled={isFormDisabled}
              >
                {t('step.selectTables.submitButton.skipBootstrapping')}
              </YBButton>
            )}
        </>
      }
      {...modalProps}
    >
      <FormProvider {...formMethods}>
        <CurrentFormStep
          currentFormStep={currentFormStep}
          isFormDisabled={isFormDisabled}
          sourceUniverse={sourceUniverse}
          tableSelectProps={{
            configAction: XClusterConfigAction.CREATE,
            isDrInterface: false,
            isTransactionalConfig: isTransactionalConfig,
            initialNamespaceUuids: [],
            selectedNamespaceUuids: selectedNamespaceUuids,
            selectedTableUuids: selectedTableUuids,
            selectionError: tableSelectionError,
            selectionWarning: tableSelectionWarning,
            setSelectedNamespaceUuids: setSelectedNamespaceUuids,
            setSelectedTableUuids: setSelectedTableUuids,
            sourceUniverseUuid: sourceUniverseUuid,
            tableType: tableType,
            targetUniverseUuid: targetUniverseUuid
          }}
        />
      </FormProvider>
    </YBModal>
  );
};

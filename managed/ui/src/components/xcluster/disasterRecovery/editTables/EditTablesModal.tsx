import { useState } from 'react';
import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { AxiosError } from 'axios';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';

import {
  editXClusterConfigTables,
  fetchTablesInUniverse,
  fetchTaskUntilItCompletes,
  fetchUniverseDiskUsageMetric,
  fetchXClusterConfig
} from '../../../../actions/xClusterReplication';
import { YBButton, YBModal, YBModalProps } from '../../../../redesign/components';
import {
  api,
  drConfigQueryKey,
  runtimeConfigQueryKey,
  universeQueryKey,
  xClusterQueryKey
} from '../../../../redesign/helpers/api';
import { assertUnreachableCase, handleServerError } from '../../../../utils/errorHandlingUtils';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import {
  BOOTSTRAP_MIN_FREE_DISK_SPACE_GB,
  XClusterConfigAction,
  XClusterConfigType,
  XCLUSTER_UNIVERSE_TABLE_FILTERS
} from '../../constants';
import {
  getTablesForBootstrapping,
  getXClusterConfigTableType,
  parseFloatIfDefined,
  shouldAutoIncludeIndexTables
} from '../../ReplicationUtils';
import { StorageConfigOption } from '../../sharedComponents/ReactSelectStorageConfig';
import { CurrentFormStep } from './CurrentFormStep';
import { getTableUuid } from '../../../../utils/tableUtils';
import { RuntimeConfigKey } from '../../../../redesign/helpers/constants';

import { TableType, Universe, UniverseNamespace, YBTable } from '../../../../redesign/helpers/dtos';
import { XClusterConfig } from '../../dtos';

import toastStyles from '../../../../redesign/styles/toastStyles.module.scss';

interface CommonEditTablesModalProps {
  xClusterConfigUuid: string;
  modalProps: YBModalProps;
}

type EditTablesModalProps =
  | (CommonEditTablesModalProps & {
      isDrInterface: true;
      drConfigUuid: string;
      storageConfigUuid: string;
    })
  | (CommonEditTablesModalProps & { isDrInterface: false });

export interface EditTablesFormValues {
  tableUuids: string[];
  namespaceUuids: string[];

  storageConfig: StorageConfigOption;
}

export const FormStep = {
  SELECT_TABLES: 'selectTables',
  CONFIGURE_BOOTSTRAP: 'configureBootstrap'
} as const;
export type FormStep = typeof FormStep[keyof typeof FormStep];

const useStyles = makeStyles(() => ({
  secondarySubmitButton: {
    marginLeft: 'auto'
  }
}));

const MODAL_NAME = 'EditTablesModal';
const TRANSLATION_KEY_PREFIX_QUERY_ERROR = 'queryError';
const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.config.editTablesModal';
const TRANSLATION_KEY_PREFIX_SELECT_TABLE = 'clusterDetail.xCluster.selectTable';
const TRANSLATION_KEY_PREFIX_XCLUSTER = 'clusterDetail.xCluster';
const FIRST_FORM_STEP = FormStep.SELECT_TABLES;

export const EditTablesModal = (props: EditTablesModalProps) => {
  const [currentFormStep, setCurrentFormStep] = useState<FormStep>(FormStep.SELECT_TABLES);
  const [selectionError, setSelectionError] = useState<{ title: string; body: string } | null>(
    null
  );
  const [selectionWarning, setSelectionWarning] = useState<{
    title: string;
    body: string;
  } | null>(null);
  const [bootstrapRequiredTableUUIDs, setBootstrapRequiredTableUUIDs] = useState<string[]>([]);
  const [isTableSelectionValidated, setIsTableSelectionValidated] = useState<boolean>(false);
  const [skipBootstrapping, setSkipBootStrapping] = useState<boolean>(false);

  const classes = useStyles();
  const theme = useTheme();
  const queryClient = useQueryClient();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const formMethods = useForm<EditTablesFormValues>({
    defaultValues: {}
  });

  const { modalProps, xClusterConfigUuid } = props;

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
      ).then((response) => response.data)
  );
  const sourceUniverseNamespacesQuery = useQuery<UniverseNamespace[]>(
    universeQueryKey.namespaces(xClusterConfigQuery.data?.sourceUniverseUUID),
    () => api.fetchUniverseNamespaces(xClusterConfigQuery.data?.sourceUniverseUUID)
  );
  const customerUuid = localStorage.getItem('customerId') ?? '';
  const runtimeConfigQuery = useQuery(runtimeConfigQueryKey.customerScope(customerUuid), () =>
    api.fetchRuntimeConfigs(customerUuid, true)
  );

  const editTableMutation = useMutation(
    (formValues: EditTablesFormValues) => {
      return props.isDrInterface
        ? api.updateTablesInDr(props.drConfigUuid, {
            tables: formValues.tableUuids
          })
        : editXClusterConfigTables(xClusterConfigUuid, {
            tables: formValues.tableUuids,
            autoIncludeIndexTables: shouldAutoIncludeIndexTables(xClusterConfigQuery.data),
            ...(!skipBootstrapping &&
              bootstrapRequiredTableUUIDs.length > 0 && {
                bootstrapParams: {
                  tables: bootstrapRequiredTableUUIDs,
                  allowBootstrap: true,
                  backupRequestParams: {
                    storageConfigUUID: formValues.storageConfig.value.uuid
                  }
                }
              })
          });
    },
    {
      onSuccess: (response) => {
        const invalidateQueries = () => {
          if (props.isDrInterface) {
            queryClient.invalidateQueries(drConfigQueryKey.detail(props.drConfigUuid));
          }
          queryClient.invalidateQueries(xClusterQueryKey.detail(xClusterConfigUuid));
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
          invalidateQueries();
        };

        modalProps.onClose();
        toast.success(
          <Typography variant="body2" component="span">
            {t('success.requestSuccess')}
          </Typography>
        );
        fetchTaskUntilItCompletes(response.taskUUID, handleTaskCompletion, invalidateQueries);
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, { customErrorLabel: t('error.requestFailureLabel') })
    }
  );
  const modalTitle = t('title');
  if (
    xClusterConfigQuery.isLoading ||
    xClusterConfigQuery.isIdle ||
    sourceUniverseQuery.isLoading ||
    sourceUniverseQuery.isIdle ||
    sourceUniverseTablesQuery.isLoading ||
    sourceUniverseTablesQuery.isIdle ||
    sourceUniverseNamespacesQuery.isLoading ||
    sourceUniverseNamespacesQuery.isIdle ||
    runtimeConfigQuery.isLoading ||
    runtimeConfigQuery.isIdle
  ) {
    return (
      <YBModal title={modalTitle} submitTestId={`${MODAL_NAME}-SubmitButton`} {...modalProps}>
        <YBLoading />
      </YBModal>
    );
  }

  if (xClusterConfigQuery.isError) {
    return (
      <YBModal title={modalTitle} submitTestId={`${MODAL_NAME}-SubmitButton`} {...modalProps}>
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

  const xClusterConfigTableType = getXClusterConfigTableType(
    xClusterConfig,
    sourceUniverseTablesQuery.data
  );
  const sourceUniverseUuid = xClusterConfig.sourceUniverseUUID;
  const targetUniverseUuid = xClusterConfig.targetUniverseUUID;
  if (
    !sourceUniverseUuid ||
    !targetUniverseUuid ||
    sourceUniverseQuery.isError ||
    sourceUniverseTablesQuery.isError ||
    sourceUniverseNamespacesQuery.isError ||
    !xClusterConfigTableType ||
    runtimeConfigQuery.isError
  ) {
    const errorMessage = !xClusterConfig.sourceUniverseUUID
      ? t('error.undefinedSourceUniverseUuid')
      : !xClusterConfig.targetUniverseUUID
      ? t('error.undefinedTargetUniverseUuid')
      : !xClusterConfigTableType
      ? t('error.undefinedXClusterTableType', { keyPrefix: TRANSLATION_KEY_PREFIX_XCLUSTER })
      : runtimeConfigQuery.isError
      ? t('failedToFetchCustomerRuntimeConfig', { keyPrefix: 'queryError' })
      : t('error.fetchSourceUniverseDetailsFailure');
    return (
      <YBModal title={modalTitle} submitTestId={`${MODAL_NAME}-SubmitButton`} {...modalProps}>
        <YBErrorIndicator customErrorMessage={errorMessage} />
      </YBModal>
    );
  }

  const sourceUniverseTables = sourceUniverseTablesQuery.data;
  const sourceUniverseNamespaces = sourceUniverseNamespacesQuery.data;
  const {
    defaultSelectedTableUuids,
    defaultSelectedNamespaceUuids,
    sourceDroppedTableUuids,
    unreplicatedTableInReplicatedNamespace
  } = classifyTablesAndNamespaces(xClusterConfig, sourceUniverseTables, sourceUniverseNamespaces);

  if (
    formMethods.formState.defaultValues &&
    Object.keys(formMethods.formState.defaultValues).length === 0
  ) {
    // react-hook-form caches the defaultValues on first render.
    // We need to update the defaultValues with reset() after the API queries are successful.
    formMethods.reset(
      getDefaultFormValues(defaultSelectedTableUuids, defaultSelectedNamespaceUuids)
    );
  }
  /**
   * Clear any existing table selection errors/warnings
   * The new table/namespace selection will need to be (re)validated.
   */
  const clearTableSelectionFeedback = () => {
    setSelectionError(null);
    setSelectionWarning(null);
    setIsTableSelectionValidated(false);
  };

  const setSelectedNamespaceUuids = (namespaces: string[]) => {
    // Clear any existing errors.
    // The new table/namespace selection will need to be (re)validated.
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

  const sourceUniverse = sourceUniverseQuery.data;
  const onSubmit = async (formValues: EditTablesFormValues, skipBootstrapping: boolean) => {
    switch (currentFormStep) {
      case FormStep.SELECT_TABLES: {
        setSelectionError(null);
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
          setSelectionError({
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
          return editTableMutation.mutateAsync(formValues);
        }

        if (!isTableSelectionValidated) {
          let bootstrapTableUuids: string[] | null = null;
          const hasSelectionError = false;

          const tableUuidsToAdd = formValues.tableUuids.filter(
            (tableUuid) => !xClusterConfig.tables.includes(tableUuid)
          );
          if (tableUuidsToAdd.length) {
            try {
              bootstrapTableUuids = await getTablesForBootstrapping(
                tableUuidsToAdd,
                sourceUniverseUuid,
                targetUniverseUuid,
                sourceUniverseTables,
                xClusterConfig.type
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
              setSelectionWarning({
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
              setSelectionWarning({
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
          return;
        } else {
          return editTableMutation.mutateAsync(formValues);
        }
      }
      case FormStep.CONFIGURE_BOOTSTRAP:
        return editTableMutation.mutateAsync(formValues);
      default:
        return assertUnreachableCase(currentFormStep);
    }
  };
  const onFormSubmit: SubmitHandler<EditTablesFormValues> = async (formValues) =>
    onSubmit(formValues, false);
  const onSkipBootstrapAndSubmit: SubmitHandler<EditTablesFormValues> = async (formValues) =>
    onSubmit(formValues, true);

  const handleBackNavigation = () => {
    switch (currentFormStep) {
      case FormStep.SELECT_TABLES:
        return;
      case FormStep.CONFIGURE_BOOTSTRAP:
        setCurrentFormStep(FormStep.SELECT_TABLES);
        return;
      default:
        assertUnreachableCase(currentFormStep);
    }
  };

  const getSubmitLabel = () => {
    switch (currentFormStep) {
      case FormStep.SELECT_TABLES:
        return isTableSelectionValidated
          ? bootstrapRequiredTableUUIDs.length > 0
            ? t(
                `step.selectTables.submitButton.configureBootstrap.${
                  props.isDrInterface ? 'dr' : 'xCluster'
                }`
              )
            : t('applyChanges', { keyPrefix: 'common' })
          : t('step.selectTables.submitButton.validateSelection');
      case FormStep.CONFIGURE_BOOTSTRAP:
        return t('applyChanges', { keyPrefix: 'common' });
      default:
        return assertUnreachableCase(currentFormStep);
    }
  };

  const submitLabel = getSubmitLabel();
  const selectedTableUuids = formMethods.watch('tableUuids');
  const selectedNamespaceUuids = formMethods.watch('namespaceUuids');
  const isFormDisabled = formMethods.formState.isSubmitting;
  const runtimeConfigEntries = runtimeConfigQuery.data.configEntries ?? [];
  const isSkipBootstrappingEnabled = runtimeConfigEntries.some(
    (config: any) =>
      config.key === RuntimeConfigKey.ENABLE_XCLUSTER_SKIP_BOOTSTRAPPING && config.value === 'true'
  );
  const isBootstrapStepRequired = bootstrapRequiredTableUUIDs.length > 0;
  return (
    <YBModal
      title={modalTitle}
      submitLabel={submitLabel}
      buttonProps={{ primary: { disabled: isFormDisabled } }}
      onSubmit={formMethods.handleSubmit(onFormSubmit)}
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      isSubmitting={formMethods.formState.isSubmitting}
      showSubmitSpinner={currentFormStep === FormStep.SELECT_TABLES || !skipBootstrapping}
      maxWidth="xl"
      size={currentFormStep === FormStep.SELECT_TABLES ? 'fit' : 'md'}
      overrideWidth="960px"
      footerAccessory={
        <>
          {currentFormStep !== FIRST_FORM_STEP && (
            <YBButton variant="secondary" onClick={handleBackNavigation}>
              {t('back', { keyPrefix: 'common' })}
            </YBButton>
          )}
          {currentFormStep === FormStep.SELECT_TABLES &&
            !props.isDrInterface &&
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
          isDrInterface={props.isDrInterface}
          storageConfigUuid={props.isDrInterface ? props.storageConfigUuid : undefined}
          tableSelectProps={{
            configAction: XClusterConfigAction.MANAGE_TABLE,
            isDrInterface: props.isDrInterface,
            selectedNamespaceUuids: selectedNamespaceUuids,
            selectedTableUuids: selectedTableUuids,
            selectionError,
            selectionWarning,
            initialNamespaceUuids: defaultSelectedNamespaceUuids ?? [],
            setSelectedNamespaceUuids: setSelectedNamespaceUuids,
            setSelectedTableUuids: setSelectedTableUuids,
            sourceUniverseUuid: sourceUniverseUuid,
            tableType: xClusterConfigTableType,
            targetUniverseUuid: targetUniverseUuid,
            xClusterConfigUuid: xClusterConfig.uuid,
            isTransactionalConfig: xClusterConfig.type === XClusterConfigType.TXN,
            sourceDroppedTableUuids: sourceDroppedTableUuids,
            unreplicatedTableInReplicatedNamespace: unreplicatedTableInReplicatedNamespace
          }}
        />
      </FormProvider>
    </YBModal>
  );
};

const getDefaultFormValues = (
  selectedTableUUIDs: string[],
  selectedNamespaceUuid: string[]
): Partial<EditTablesFormValues> => {
  return {
    tableUuids: selectedTableUUIDs,
    namespaceUuids: selectedNamespaceUuid
  };
};

export const classifyTablesAndNamespaces = (
  xClusterConfig: XClusterConfig,
  sourceUniverseTables: YBTable[],
  sourceUniverseNamespaces: UniverseNamespace[]
) => {
  const selectedTableUuids = new Set<string>();
  const sourceDroppedTableUuids = new Set<string>();
  const selectedNamespaceUuid = new Set<string>();
  const unreplicatedTableInReplicatedNamespace = new Set<string>();
  const tableUuidToTable = Object.fromEntries(
    sourceUniverseTables.map((table) => [getTableUuid(table), table])
  );
  const namespaceToNamespaceUuid = Object.fromEntries(
    sourceUniverseNamespaces.map((namespace) => [namespace.name, namespace.namespaceUUID])
  );

  // Classify every table as selected or dropped by checking for a match on the source universe.
  xClusterConfig.tables.forEach((tableUuid) => {
    const sourceUniverseTable = tableUuidToTable[tableUuid];

    if (sourceUniverseTable) {
      // The xCluster config table still exists on the source universe.
      selectedTableUuids.add(tableUuid);
      selectedNamespaceUuid.add(namespaceToNamespaceUuid[sourceUniverseTable.keySpace]);
    } else {
      sourceDroppedTableUuids.add(tableUuid);
    }
  });

  // Find all the unreplicated tables which belong in a namespace that is being replicated.
  // These are of interest in the YSQL case because all tables in a namespace should be replicated to
  // avoid issues with backup and restore which is limited to DB scope.
  // The backup and restore limitation is not present for YCQL.
  sourceUniverseTables.forEach((sourceUniverseTable) => {
    if (sourceUniverseTable.tableType === TableType.PGSQL_TABLE_TYPE) {
      const sourceUniverseTableUuid = getTableUuid(sourceUniverseTable);
      const sourceUniverseNamespaceUuid = namespaceToNamespaceUuid[sourceUniverseTable.keySpace];
      if (
        !selectedTableUuids.has(sourceUniverseTableUuid) &&
        selectedNamespaceUuid.has(sourceUniverseNamespaceUuid)
      ) {
        unreplicatedTableInReplicatedNamespace.add(sourceUniverseTableUuid);
      }
    }
  });

  return {
    defaultSelectedTableUuids: Array.from(selectedTableUuids),
    defaultSelectedNamespaceUuids: Array.from(selectedNamespaceUuid),
    sourceDroppedTableUuids: sourceDroppedTableUuids,
    unreplicatedTableInReplicatedNamespace: unreplicatedTableInReplicatedNamespace
  };
};

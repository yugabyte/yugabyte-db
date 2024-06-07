import { useState } from 'react';
import { Box, Typography, useTheme } from '@material-ui/core';
import { AxiosError } from 'axios';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';

import {
  editXClusterConfigTables,
  fetchTablesInUniverse,
  fetchTaskUntilItCompletes,
  fetchUniverseDiskUsageMetric
} from '../../../../actions/xClusterReplication';
import { YBButton, YBModal, YBModalProps } from '../../../../redesign/components';
import {
  api,
  drConfigQueryKey,
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
  formatUuidForXCluster,
  getTablesForBootstrapping,
  getXClusterConfigTableType,
  parseFloatIfDefined,
  shouldAutoIncludeIndexTables
} from '../../ReplicationUtils';
import { StorageConfigOption } from '../../sharedComponents/ReactSelectStorageConfig';
import { CurrentFormStep } from './CurrentFormStep';
import { getTableUuid } from '../../../../utils/tableUtils';

import { Universe, UniverseNamespace, YBTable } from '../../../../redesign/helpers/dtos';
import { XClusterConfig } from '../../dtos';

import toastStyles from '../../../../redesign/styles/toastStyles.module.scss';

interface CommonEditTablesModalProps {
  xClusterConfig: XClusterConfig;
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

  storageConfig?: StorageConfigOption;
}

export const FormStep = {
  SELECT_TABLES: 'selectTables',
  CONFIGURE_BOOTSTRAP: 'configureBootstrap'
} as const;
export type FormStep = typeof FormStep[keyof typeof FormStep];

const FIRST_FORM_STEP = FormStep.SELECT_TABLES;
const MODAL_NAME = 'EditTablesModal';
const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.config.editTablesModal';
const TRANSLATION_KEY_PREFIX_SELECT_TABLE = 'clusterDetail.xCluster.selectTable';
const TRANSLATION_KEY_PREFIX_XCLUSTER = 'clusterDetail.xCluster';

export const EditTablesModal = (props: EditTablesModalProps) => {
  const [currentFormStep, setCurrentFormStep] = useState<FormStep>(FIRST_FORM_STEP);
  const [selectionError, setSelectionError] = useState<{ title: string; body: string }>();
  const [selectionWarning, setSelectionWarning] = useState<{ title: string; body: string }>();
  const [bootstrapRequiredTableUUIDs, setBootstrapRequiredTableUUIDs] = useState<string[]>([]);
  const [isTableSelectionValidated, setIsTableSelectionValidated] = useState<boolean>(false);

  const theme = useTheme();
  const queryClient = useQueryClient();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const { modalProps, xClusterConfig } = props;
  const sourceUniverseQuery = useQuery<Universe>(
    universeQueryKey.detail(xClusterConfig.sourceUniverseUUID),
    () => api.fetchUniverse(xClusterConfig.sourceUniverseUUID)
  );

  const sourceUniverseTablesQuery = useQuery<YBTable[]>(
    universeQueryKey.tables(xClusterConfig.sourceUniverseUUID, XCLUSTER_UNIVERSE_TABLE_FILTERS),
    () =>
      fetchTablesInUniverse(
        xClusterConfig.sourceUniverseUUID,
        XCLUSTER_UNIVERSE_TABLE_FILTERS
      ).then((response) => response.data)
  );

  const sourceUniverseNamespacesQuery = useQuery<UniverseNamespace[]>(
    universeQueryKey.namespaces(xClusterConfig.sourceUniverseUUID),
    () => api.fetchUniverseNamespaces(xClusterConfig.sourceUniverseUUID)
  );

  const editTableMutation = useMutation(
    (formValues: EditTablesFormValues) => {
      const bootstrapParams =
        bootstrapRequiredTableUUIDs.length > 0
          ? {
              tables: bootstrapRequiredTableUUIDs,
              backupRequestParams: {
                storageConfigUUID: formValues.storageConfig?.value.uuid
              }
            }
          : undefined;
      return props.isDrInterface
        ? api.updateTablesInDr(props.drConfigUuid, {
            tables: formValues.tableUuids
          })
        : editXClusterConfigTables(xClusterConfig.uuid, {
            tables: formValues.tableUuids,
            autoIncludeIndexTables: shouldAutoIncludeIndexTables(xClusterConfig),
            bootstrapParams: bootstrapParams
          });
    },
    {
      onSuccess: (response) => {
        const invalidateQueries = () => {
          if (props.isDrInterface) {
            queryClient.invalidateQueries(drConfigQueryKey.detail(props.drConfigUuid));
          }
          queryClient.invalidateQueries(xClusterQueryKey.detail(xClusterConfig.uuid));
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

  const defaultValues =
    sourceUniverseTablesQuery.data && sourceUniverseNamespacesQuery.data
      ? getDefaultFormValues(
          xClusterConfig,
          sourceUniverseTablesQuery.data,
          sourceUniverseNamespacesQuery.data
        )
      : {};
  const formMethods = useForm<EditTablesFormValues>({
    defaultValues: defaultValues
  });

  const modalTitle = t('title');
  const cancelLabel = t('cancel', { keyPrefix: 'common' });
  if (
    sourceUniverseQuery.isLoading ||
    sourceUniverseQuery.isIdle ||
    sourceUniverseTablesQuery.isLoading ||
    sourceUniverseTablesQuery.isIdle ||
    sourceUniverseNamespacesQuery.isLoading ||
    sourceUniverseNamespacesQuery.isIdle
  ) {
    return (
      <YBModal
        title={modalTitle}
        cancelLabel={cancelLabel}
        submitTestId={`${MODAL_NAME}-SubmitButton`}
        cancelTestId={`${MODAL_NAME}-CancelButton`}
        {...modalProps}
      >
        <YBLoading />
      </YBModal>
    );
  }

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
    !xClusterConfigTableType
  ) {
    const errorMessage = !xClusterConfig.sourceUniverseUUID
      ? t('error.undefinedSourceUniverseUuid')
      : !xClusterConfig.targetUniverseUUID
      ? t('error.undefinedTargetUniverseUuid')
      : !xClusterConfigTableType
      ? t('error.undefinedXClusterTableType', { keyPrefix: TRANSLATION_KEY_PREFIX_XCLUSTER })
      : t('error.fetchSourceUniverseDetailsFailure');
    return (
      <YBModal
        title={modalTitle}
        cancelLabel={cancelLabel}
        submitTestId={`${MODAL_NAME}-SubmitButton`}
        cancelTestId={`${MODAL_NAME}-CancelButton`}
        {...modalProps}
      >
        <YBErrorIndicator customErrorMessage={errorMessage} />
      </YBModal>
    );
  }

  const sourceUniverseTables = sourceUniverseTablesQuery.data;
  const sourceUniverseNamespaces = sourceUniverseNamespacesQuery.data;
  if (
    formMethods.formState.defaultValues &&
    Object.keys(formMethods.formState.defaultValues).length === 0
  ) {
    // react-hook-form caches the defaultValues on first render.
    // We need to update the defaultValues with reset() after the API queries are successful.
    formMethods.reset(
      getDefaultFormValues(xClusterConfig, sourceUniverseTables, sourceUniverseNamespaces)
    );
  }

  const sourceUniverse = sourceUniverseQuery.data;
  const onSubmit: SubmitHandler<EditTablesFormValues> = async (formValues) => {
    switch (currentFormStep) {
      case FormStep.SELECT_TABLES: {
        setSelectionError(undefined);
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

        if (!isTableSelectionValidated) {
          let bootstrapTableUuids: string[] | null = null;
          const hasSelectionError = false;

          const tableUuidsToAdd = formValues.tableUuids.filter(
            (tableUuid) => !props.xClusterConfig.tables.includes(tableUuid)
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

            // Validate that the source universe has at least the recommeneded amount of
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

  const setSelectedNamespaceUuids = (namespaces: string[]) => {
    // Clear any existing errors.
    // The new table/namespace selection will need to be (re)validated.
    setIsTableSelectionValidated(false);
    formMethods.clearErrors('namespaceUuids');

    // We will run any required validation on selected namespaces & tables all at once when the
    // user clicks on the 'Validate Selection' button.
    formMethods.setValue('namespaceUuids', namespaces, { shouldValidate: false });
  };
  const setSelectedTableUuids = (tableUuids: string[]) => {
    // Clear any existing errors.
    // The new table/namespace selection will need to be (re)validated.
    setIsTableSelectionValidated(false);
    formMethods.clearErrors('tableUuids');

    // We will run any required validation on selected namespaces & tables all at once when the
    // user clicks on the 'Validate Selection' button.
    formMethods.setValue('tableUuids', tableUuids, { shouldValidate: false });
  };

  const handleBackNavigation = () => {
    switch (currentFormStep) {
      case FIRST_FORM_STEP:
        return;
      case FormStep.CONFIGURE_BOOTSTRAP:
        setCurrentFormStep(FormStep.SELECT_TABLES);
        return;
      default:
        assertUnreachableCase(currentFormStep);
    }
  };

  const getSubmitlabel = () => {
    switch (currentFormStep) {
      case FormStep.SELECT_TABLES:
        return isTableSelectionValidated
          ? bootstrapRequiredTableUUIDs.length > 0
            ? t('step.selectTables.nextButton')
            : t('applyChanges', { keyPrefix: 'common' })
          : t('submitButton.validate');
      case FormStep.CONFIGURE_BOOTSTRAP:
        return t('applyChanges', { keyPrefix: 'common' });
      default:
        return assertUnreachableCase(currentFormStep);
    }
  };

  const submitLabel = getSubmitlabel();
  const selectedTableUuids = formMethods.watch('tableUuids');
  const selectedNamespaceUuids = formMethods.watch('namespaceUuids');
  const isFormDisabled = formMethods.formState.isSubmitting;
  return (
    <YBModal
      title={modalTitle}
      submitLabel={submitLabel}
      cancelLabel={cancelLabel}
      buttonProps={{ primary: { disabled: isFormDisabled } }}
      onSubmit={formMethods.handleSubmit(onSubmit)}
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      cancelTestId={`${MODAL_NAME}-CancelButton`}
      isSubmitting={formMethods.formState.isSubmitting}
      maxWidth="xl"
      size={currentFormStep === FormStep.SELECT_TABLES ? 'fit' : 'md'}
      overrideWidth="960px"
      footerAccessory={
        currentFormStep !== FIRST_FORM_STEP && (
          <YBButton variant="secondary" onClick={handleBackNavigation}>
            {t('back', { keyPrefix: 'common' })}
          </YBButton>
        )
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
            isFixedTableType: true, // Users are not allowed to change xCluster table type after creation.
            selectedNamespaceUuids: selectedNamespaceUuids,
            selectedTableUUIDs: selectedTableUuids,
            selectionError,
            selectionWarning,
            initialNamespaceUuids: defaultValues.namespaceUuids ?? [],
            setSelectedNamespaceUuids: setSelectedNamespaceUuids,
            setSelectedTableUUIDs: setSelectedTableUuids,
            setTableType: (_) => null, // Users are not allowed to change xCluster table type after creation.
            sourceUniverseUUID: sourceUniverseUuid,
            tableType: xClusterConfigTableType,
            targetUniverseUUID: targetUniverseUuid,
            xClusterConfigUUID: xClusterConfig.uuid,
            isTransactionalConfig: xClusterConfig.type === XClusterConfigType.TXN
          }}
        />
      </FormProvider>
    </YBModal>
  );
};

const getXClusterConfigNamespaces = (
  xClusterConfig: XClusterConfig,
  sourceUniverseTables: YBTable[],
  sourceUniverseNamespaces: UniverseNamespace[]
): string[] => {
  const namespaceToNamespaceUuid = Object.fromEntries(
    sourceUniverseNamespaces.map((namespace) => [namespace.name, namespace.namespaceUUID])
  );

  const selectedTableUuids = new Set<string>(xClusterConfig.tables);
  const selectedNamespaceUuid = new Set<string>();
  sourceUniverseTables.forEach((table) => {
    if (selectedTableUuids.has(formatUuidForXCluster(getTableUuid(table)))) {
      selectedNamespaceUuid.add(namespaceToNamespaceUuid[table.keySpace]);
    }
  });
  return Array.from(selectedNamespaceUuid);
};

const getDefaultSelectedTableUuids = (
  xClusterConfig: XClusterConfig,
  sourceUniverseTables: YBTable[]
): string[] => {
  const defaultSelectedTableUuids = new Set<string>();
  const tableUuidToTable = Object.fromEntries(
    sourceUniverseTables.map((table) => [getTableUuid(table), table])
  );
  xClusterConfig.tables.forEach((tableUuid) => {
    const sourceUniverseTable = tableUuidToTable[tableUuid];
    if (sourceUniverseTable) {
      // The xCluster config table still exists in the source universe.
      defaultSelectedTableUuids.add(tableUuid);
    }
  });
  return Array.from(defaultSelectedTableUuids);
};

const getDefaultFormValues = (
  xClusterConfig: XClusterConfig,
  sourceUniverseTables: YBTable[],
  sourceUniverseNamespace: UniverseNamespace[]
): Partial<EditTablesFormValues> => {
  return {
    tableUuids: getDefaultSelectedTableUuids(xClusterConfig, sourceUniverseTables),
    namespaceUuids: getXClusterConfigNamespaces(
      xClusterConfig,
      sourceUniverseTables,
      sourceUniverseNamespace
    )
  };
};

import { useState } from 'react';
import { Box, Typography, useTheme } from '@material-ui/core';
import { AxiosError } from 'axios';
import { FormProvider, useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';

import {
  editXClusterConfigTables,
  fetchTaskUntilItCompletes,
  fetchXClusterConfig,
  isBootstrapRequired
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
import { XClusterConfigAction, XClusterTableStatus } from '../../constants';
import {
  getCategorizedNeedBootstrapPerTableResponse,
  getInConfigTableUuidsToTableDetailsMap,
  getXClusterConfigTableType,
  shouldAutoIncludeIndexTables
} from '../../ReplicationUtils';
import { StorageConfigOption } from '../../sharedComponents/ReactSelectStorageConfig';
import { CurrentFormStep } from './CurrentFormStep';
import { getTableUuid } from '../../../../utils/tableUtils';

import { TableType, Universe, UniverseNamespace } from '../../../../redesign/helpers/dtos';
import { XClusterConfig, XClusterConfigNeedBootstrapPerTableResponse } from '../../dtos';
import { CategorizedNeedBootstrapPerTableResponse } from '../../XClusterTypes';

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
  namespaceUuids: string[];
  tableUuids: string[];
  storageConfig: StorageConfigOption;
  skipBootstrap: boolean;
}

export const FormStep = {
  SELECT_TABLES: 'selectTables',
  BOOTSTRAP_SUMMARY: 'bootstrapSummary'
} as const;
export type FormStep = typeof FormStep[keyof typeof FormStep];

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
  const [
    categorizedNeedBootstrapPerTableResponse,
    setCategorizedNeedBootstrapPerTableResponse
  ] = useState<CategorizedNeedBootstrapPerTableResponse | null>(null);
  const [isTableSelectionValidated, setIsTableSelectionValidated] = useState<boolean>(false);

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
      const bootstrapRequiredTableUuids =
        categorizedNeedBootstrapPerTableResponse?.bootstrapTableUuids ?? [];
      return props.isDrInterface
        ? api.updateTablesInDr(props.drConfigUuid, {
            tables: formValues.tableUuids
          })
        : editXClusterConfigTables(xClusterConfigUuid, {
            tables: formValues.tableUuids,
            autoIncludeIndexTables: shouldAutoIncludeIndexTables(xClusterConfigQuery.data),
            ...(!formValues.skipBootstrap &&
              bootstrapRequiredTableUuids.length > 0 && {
                bootstrapParams: {
                  tables: bootstrapRequiredTableUuids,
                  allowBootstrap: true,
                  backupRequestParams: {
                    storageConfigUUID: formValues.storageConfig?.value.uuid ?? ''
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

  const xClusterConfigTableType = getXClusterConfigTableType(xClusterConfig);
  const sourceUniverseUuid = xClusterConfig.sourceUniverseUUID;
  const targetUniverseUuid = xClusterConfig.targetUniverseUUID;
  if (
    !sourceUniverseUuid ||
    !targetUniverseUuid ||
    sourceUniverseQuery.isError ||
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

  const sourceUniverseNamespaces = sourceUniverseNamespacesQuery.data;
  const {
    defaultSelectedTableUuids,
    defaultSelectedNamespaceUuids,
    unreplicatedTableInReplicatedNamespace,
    tableUuidsDroppedOnSource,
    tableUuidsDroppedOnTarget
  } = classifyTablesAndNamespaces(xClusterConfig, sourceUniverseNamespaces);

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

  const onSubmit = async (formValues: EditTablesFormValues) => {
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

        if (!isTableSelectionValidated) {
          let xClusterConfigNeedBootstrapPerTableResponse: XClusterConfigNeedBootstrapPerTableResponse = {};
          const hasSelectionError = false;

          const tableUuidToTableDetails = getInConfigTableUuidsToTableDetailsMap(
            xClusterConfig.tableDetails
          );
          const tableUuidsToAdd = formValues.tableUuids.filter(
            (tableUuid) => !tableUuidToTableDetails.has(tableUuid)
          );
          if (tableUuidsToAdd.length) {
            try {
              xClusterConfigNeedBootstrapPerTableResponse = await isBootstrapRequired(
                sourceUniverseUuid,
                targetUniverseUuid,
                tableUuidsToAdd,
                xClusterConfig.type,
                true /* includeDetails */
              );
              const categorizedNeedBootstrapPerTableResponse = getCategorizedNeedBootstrapPerTableResponse(
                xClusterConfigNeedBootstrapPerTableResponse
              );

              setCategorizedNeedBootstrapPerTableResponse(categorizedNeedBootstrapPerTableResponse);
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
          } else {
            // If we're not adding tables, we should clear the need bootstrap response so bootstrapRequiredUuids is empty.
            setCategorizedNeedBootstrapPerTableResponse(null);
          }

          if (hasSelectionError === false) {
            setIsTableSelectionValidated(true);
          }
          return tableUuidsToAdd.length > 0
            ? setCurrentFormStep(FormStep.BOOTSTRAP_SUMMARY)
            : editTableMutation.mutateAsync(formValues);
        }
        return tableUuidsToAdd.length > 0
          ? setCurrentFormStep(FormStep.BOOTSTRAP_SUMMARY)
          : editTableMutation.mutateAsync(formValues);
      }
      case FormStep.BOOTSTRAP_SUMMARY:
        return editTableMutation.mutateAsync(formValues);
      default:
        return assertUnreachableCase(currentFormStep);
    }
  };

  const handleBackNavigation = () => {
    switch (currentFormStep) {
      case FormStep.SELECT_TABLES:
        return;
      case FormStep.BOOTSTRAP_SUMMARY:
        setCurrentFormStep(FormStep.SELECT_TABLES);
        return;
      default:
        assertUnreachableCase(currentFormStep);
    }
  };

  const selectedTableUuids = formMethods.watch('tableUuids');
  const isFormDisabled = formMethods.formState.isSubmitting;
  const tableUuidToTableDetails = getInConfigTableUuidsToTableDetailsMap(
    xClusterConfig.tableDetails
  );
  const tableUuidsToAdd = selectedTableUuids.filter(
    (tableUuid) => !tableUuidToTableDetails.has(tableUuid)
  );
  const getSubmitLabel = () => {
    switch (currentFormStep) {
      case FormStep.SELECT_TABLES:
        return tableUuidsToAdd.length > 0
          ? t(
              `step.selectTables.submitButton.bootstrapSummary.${
                props.isDrInterface ? 'dr' : 'xCluster'
              }`
            )
          : t('applyChanges', { keyPrefix: 'common' });
      case FormStep.BOOTSTRAP_SUMMARY:
        return t('applyChanges', { keyPrefix: 'common' });
      default:
        return assertUnreachableCase(currentFormStep);
    }
  };

  const submitLabel = getSubmitLabel();
  const selectedNamespaceUuids = formMethods.watch('namespaceUuids');

  const formStepProps = {
    currentFormStep,
    isFormDisabled,
    sourceUniverseUuid: sourceUniverseUuid,
    tableSelectProps: {
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
      xClusterConfigType: xClusterConfig.type,
      unreplicatedTableInReplicatedNamespace: unreplicatedTableInReplicatedNamespace,
      tableUuidsDroppedOnSource: tableUuidsDroppedOnSource,
      tableUuidsDroppedOnTarget: tableUuidsDroppedOnTarget
    },
    categorizedNeedBootstrapPerTableResponse: categorizedNeedBootstrapPerTableResponse
  };
  return (
    <YBModal
      title={modalTitle}
      submitLabel={submitLabel}
      buttonProps={{ primary: { disabled: isFormDisabled } }}
      onSubmit={formMethods.handleSubmit(onSubmit)}
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      isSubmitting={formMethods.formState.isSubmitting}
      showSubmitSpinner={currentFormStep === FormStep.SELECT_TABLES}
      maxWidth="xl"
      size={
        ([FormStep.SELECT_TABLES, FormStep.BOOTSTRAP_SUMMARY] as FormStep[]).includes(
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
        </>
      }
      {...modalProps}
    >
      <FormProvider {...formMethods}>
        <CurrentFormStep
          {...(props.isDrInterface
            ? {
                isDrInterface: true,
                storageConfigUuid: props.storageConfigUuid
              }
            : { isDrInterface: false })}
          {...formStepProps}
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
  sourceUniverseNamespaces: UniverseNamespace[]
) => {
  const selectedTableUuids = new Set<string>();
  const selectedNamespaceUuid = new Set<string>();
  const unreplicatedTableInReplicatedNamespace = new Set<string>();
  const tableUuidsDroppedOnSource = new Set<string>();
  const tableUuidsDroppedOnTarget = new Set<string>();

  const namespaceToNamespaceUuid = Object.fromEntries(
    sourceUniverseNamespaces.map((namespace) => [namespace.name, namespace.namespaceUUID])
  );

  xClusterConfig.tableDetails.forEach((tableDetail) => {
    const sourceTableInfo = tableDetail.sourceTableInfo;
    const tableUuid = tableDetail.tableId;

    // Preselect all tables which are in the xCluster config and still exist on the source universe.
    switch (tableDetail.status) {
      case XClusterTableStatus.DROPPED_FROM_SOURCE:
        tableUuidsDroppedOnSource.add(tableUuid);
        return;
      case XClusterTableStatus.DROPPED_FROM_TARGET:
        if (sourceTableInfo) {
          selectedTableUuids.add(getTableUuid(sourceTableInfo));
          selectedNamespaceUuid.add(namespaceToNamespaceUuid[sourceTableInfo.keySpace]);

          // If the main table is preselected, then we will preselect the index tables as well.
          sourceTableInfo.indexTableIDs?.forEach((indexTableId) =>
            selectedTableUuids.add(indexTableId)
          );
        }
        tableUuidsDroppedOnTarget.add(tableUuid);
        return;
      case XClusterTableStatus.DROPPED:
        tableUuidsDroppedOnSource.add(tableUuid);
        tableUuidsDroppedOnTarget.add(tableUuid);
        return;
      case XClusterTableStatus.EXTRA_TABLE_ON_SOURCE:
        // These are of interest in the YSQL case because all tables in a namespace should be replicated to
        // avoid issues with backup and restore which is limited to DB scope.
        // The backup and restore limitation is not present for YCQL.
        if (sourceTableInfo?.tableType === TableType.PGSQL_TABLE_TYPE) {
          unreplicatedTableInReplicatedNamespace.add(tableUuid);
        }
        return;
      default:
        if (sourceTableInfo) {
          selectedTableUuids.add(getTableUuid(sourceTableInfo));
          selectedNamespaceUuid.add(namespaceToNamespaceUuid[sourceTableInfo.keySpace]);

          // If the main table is preselected, then we will preselect the index tables as well.
          sourceTableInfo.indexTableIDs?.forEach((indexTableId) =>
            selectedTableUuids.add(indexTableId)
          );
        }
    }
  });

  return {
    defaultSelectedTableUuids: Array.from(selectedTableUuids),
    defaultSelectedNamespaceUuids: Array.from(selectedNamespaceUuid),
    unreplicatedTableInReplicatedNamespace,
    tableUuidsDroppedOnSource,
    tableUuidsDroppedOnTarget
  };
};

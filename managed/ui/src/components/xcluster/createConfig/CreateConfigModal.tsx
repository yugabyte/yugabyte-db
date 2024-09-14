import { useState } from 'react';
import { AxiosError } from 'axios';
import { Box, Typography, useTheme } from '@material-ui/core';
import { toast } from 'react-toastify';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { useTranslation } from 'react-i18next';
import { FormProvider, useForm } from 'react-hook-form';

import {
  createXClusterConfig,
  CreateXClusterConfigRequest,
  fetchTablesInUniverse,
  fetchTaskUntilItCompletes,
  isBootstrapRequired
} from '../../../actions/xClusterReplication';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { getCategorizedNeedBootstrapPerTableResponse } from '../ReplicationUtils';
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
  XClusterConfigAction,
  XClusterConfigType,
  XCLUSTER_UNIVERSE_TABLE_FILTERS
} from '../constants';

import { CategorizedNeedBootstrapPerTableResponse, XClusterTableType } from '../XClusterTypes';
import { TableType, TableTypeLabel, Universe, YBTable } from '../../../redesign/helpers/dtos';
import { XClusterConfigNeedBootstrapPerTableResponse } from '../dtos';

import toastStyles from '../../../redesign/styles/toastStyles.module.scss';

interface CreateConfigModalProps {
  modalProps: YBModalProps;
  sourceUniverseUuid: string;
}

export interface CreateXClusterConfigFormValues {
  configName: string;
  targetUniverse: { label: string; value: Universe; isDisabled: boolean; disabledReason?: string };
  tableType: { label: string; value: XClusterTableType };
  isTransactionalConfig: boolean;
  namespaceUuids: string[];
  tableUuids: string[];
  storageConfig: StorageConfigOption;
  skipBootstrap: boolean;
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
  BOOTSTRAP_SUMMARY: 'bootstrapSummary',
  CONFIRM_ALERT: 'configureAlert'
} as const;
export type FormStep = typeof FormStep[keyof typeof FormStep];

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
  const [
    categorizedNeedBootstrapPerTableResponse,
    setCategorizedNeedBootstrapPerTableResponse
  ] = useState<CategorizedNeedBootstrapPerTableResponse | null>(null);
  const [isTableSelectionValidated, setIsTableSelectionValidated] = useState<boolean>(false);
  // The purpose of committedTargetUniverse is to store the targetUniverse field value prior
  // to the user submitting their target universe step.
  // This value updates whenever the user submits SelectTargetUniverseStep with a new
  // target universe.
  const [committedTargetUniverseUuid, setCommittedTargetUniverseUuid] = useState<string>();
  const [committedTableType, setCommittedTableType] = useState<XClusterTableType>();

  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const queryClient = useQueryClient();
  const theme = useTheme();

  const xClusterConfigMutation = useMutation(
    (formValues: CreateXClusterConfigFormValues) => {
      const bootstrapRequiredTableUuids =
        categorizedNeedBootstrapPerTableResponse?.bootstrapTableUuids ?? [];
      const createXClusterConfigRequest: CreateXClusterConfigRequest = {
        name: formValues.configName,
        sourceUniverseUUID: sourceUniverseUuid,
        targetUniverseUUID: formValues.targetUniverse.value.universeUUID,
        configType: formValues.isTransactionalConfig
          ? XClusterConfigType.TXN
          : XClusterConfigType.BASIC,
        tables: formValues.tableUuids,

        ...(!formValues.skipBootstrap &&
          bootstrapRequiredTableUuids.length > 0 && {
            bootstrapParams: {
              tables: bootstrapRequiredTableUuids,
              allowBootstrap: true,

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

  const onSubmit = async (formValues: CreateXClusterConfigFormValues) => {
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

        if (!isTableSelectionValidated) {
          let xClusterConfigNeedBootstrapPerTableResponse: XClusterConfigNeedBootstrapPerTableResponse = {};
          const hasSelectionError = false;

          if (formValues.tableUuids.length) {
            try {
              xClusterConfigNeedBootstrapPerTableResponse = await isBootstrapRequired(
                sourceUniverseUuid,
                targetUniverseUuid,
                formValues.tableUuids,
                formValues.isTransactionalConfig
                  ? XClusterConfigType.TXN
                  : XClusterConfigType.BASIC,
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

          if (hasSelectionError === false) {
            setIsTableSelectionValidated(true);
          }

          setCurrentFormStep(FormStep.BOOTSTRAP_SUMMARY);
          return;
        }
        setCurrentFormStep(FormStep.BOOTSTRAP_SUMMARY);
        return;
      case FormStep.BOOTSTRAP_SUMMARY:
        setCurrentFormStep(FormStep.CONFIRM_ALERT);
        return;
      case FormStep.CONFIRM_ALERT:
        return xClusterConfigMutation.mutateAsync(formValues);
      default:
        return assertUnreachableCase(currentFormStep);
    }
  };
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
      case FormStep.BOOTSTRAP_SUMMARY:
        setCurrentFormStep(FormStep.SELECT_TABLES);
        return;
      case FormStep.CONFIRM_ALERT:
        setCurrentFormStep(FormStep.BOOTSTRAP_SUMMARY);
        return;
      default:
        assertUnreachableCase(currentFormStep);
    }
  };

  const tableType = formMethods.watch('tableType')?.value;
  const getFormSubmitLabel = (formStep: FormStep) => {
    switch (formStep) {
      case FormStep.SELECT_TARGET_UNIVERSE:
        return t(
          `step.selectTargetUniverse.submitButton.${
            tableType === TableType.PGSQL_TABLE_TYPE ? 'selectDatabases' : 'selectTables'
          }`
        );
      case FormStep.SELECT_TABLES:
        return t('step.selectTables.submitButton.bootstrapSummary');
      case FormStep.BOOTSTRAP_SUMMARY:
        return t('step.bootstrapSummary.submitButton');
      case FormStep.CONFIRM_ALERT:
        return t('confirmAlert.submitButton', { keyPrefix: 'clusterDetail.xCluster.shared' });
      default:
        return assertUnreachableCase(formStep);
    }
  };

  const selectedTableUuids = formMethods.watch('tableUuids');
  const selectedNamespaceUuids = formMethods.watch('namespaceUuids');
  const targetUniverseUuid = formMethods.watch('targetUniverse.value.universeUUID');
  const isTransactionalConfig = formMethods.watch('isTransactionalConfig');
  const sourceUniverse = sourceUniverseQuery.data;
  const submitLabel = getFormSubmitLabel(currentFormStep);

  const xClusterConfigType = isTransactionalConfig
    ? XClusterConfigType.TXN
    : XClusterConfigType.BASIC;
  const isFormDisabled = formMethods.formState.isSubmitting;
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
        ([
          FormStep.SELECT_TARGET_UNIVERSE,
          FormStep.SELECT_TABLES,
          FormStep.BOOTSTRAP_SUMMARY
        ] as FormStep[]).includes(currentFormStep)
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
          currentFormStep={currentFormStep}
          isFormDisabled={isFormDisabled}
          sourceUniverse={sourceUniverse}
          tableSelectProps={{
            configAction: XClusterConfigAction.CREATE,
            isDrInterface: false,
            initialNamespaceUuids: [],
            selectedNamespaceUuids: selectedNamespaceUuids,
            selectedTableUuids: selectedTableUuids,
            selectionError: tableSelectionError,
            selectionWarning: tableSelectionWarning,
            setSelectedNamespaceUuids: setSelectedNamespaceUuids,
            setSelectedTableUuids: setSelectedTableUuids,
            sourceUniverseUuid: sourceUniverseUuid,
            tableType: tableType,
            xClusterConfigType: xClusterConfigType,
            targetUniverseUuid: targetUniverseUuid
          }}
          categorizedNeedBootstrapPerTableResponse={categorizedNeedBootstrapPerTableResponse}
        />
      </FormProvider>
    </YBModal>
  );
};

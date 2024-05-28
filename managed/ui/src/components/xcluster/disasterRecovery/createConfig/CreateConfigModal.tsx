import { useState } from 'react';
import { AxiosError } from 'axios';
import { Typography } from '@material-ui/core';
import { toast } from 'react-toastify';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { useTranslation } from 'react-i18next';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';

import {
  fetchTablesInUniverse,
  fetchTaskUntilItCompletes
} from '../../../../actions/xClusterReplication';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import { formatUuidForXCluster } from '../../ReplicationUtils';

import { assertUnreachableCase, handleServerError } from '../../../../utils/errorHandlingUtils';
import {
  api,
  CreateDrConfigRequest,
  drConfigQueryKey,
  universeQueryKey
} from '../../../../redesign/helpers/api';
import { generateUniqueName, isActionFrozen } from '../../../../redesign/helpers/utils';
import { YBButton, YBModal, YBModalProps } from '../../../../redesign/components';
import { CurrentFormStep } from './CurrentFormStep';
import { StorageConfigOption } from '../../sharedComponents/ReactSelectStorageConfig';
import { AllowedTasks, TableType, Universe, YBTable } from '../../../../redesign/helpers/dtos';
import { UNIVERSE_TASKS } from '../../../../redesign/helpers/constants';
import { XClusterConfigAction, XCLUSTER_UNIVERSE_TABLE_FILTERS } from '../../constants';

import toastStyles from '../../../../redesign/styles/toastStyles.module.scss';

export interface CreateDrConfigFormValues {
  targetUniverse: { label: string; value: Universe };
  namespaceUuids: string[];
  tableUuids: string[];
  storageConfig: StorageConfigOption;
}

export interface CreateDrConfigFormErrors {
  targetUniverse: string;
  namespaceUuids: { title: string; body: string };
  storageConfig: string;
}

export interface CreateXClusterConfigFormWarnings {
  targetUniverse?: string;
  namespaceUuids?: { title: string; body: string };
  storageConfig?: string;
}

interface CreateConfigModalProps {
  modalProps: YBModalProps;
  sourceUniverseUuid: string;
  allowedTasks: AllowedTasks;
}

export const FormStep = {
  SELECT_TARGET_UNIVERSE: 'selectTargetUniverse',
  SELECT_TABLES: 'selectDatabases',
  CONFIGURE_BOOTSTRAP: 'configureBootstrap',
  CONFIRM_ALERT: 'configureAlert'
} as const;
export type FormStep = typeof FormStep[keyof typeof FormStep];

const MODAL_NAME = 'CreateConfigModal';
const FIRST_FORM_STEP = FormStep.SELECT_TARGET_UNIVERSE;
const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.config.createModal';
const SELECT_TABLE_TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.selectTable';

export const CreateConfigModal = ({
  modalProps,
  sourceUniverseUuid,
  allowedTasks
}: CreateConfigModalProps) => {
  const [currentFormStep, setCurrentFormStep] = useState<FormStep>(FIRST_FORM_STEP);
  const [tableSelectionError, setTableSelectionError] = useState<{ title: string; body: string }>();

  // The purpose of committedTargetUniverse is to store the targetUniverse field value prior
  // to the user submitting their target universe step.
  // This value updates whenever the user submits SelectTargetUniverseStep with a new
  // target universe.
  const [committedTargetUniverseUUID, setCommittedTargetUniverseUUID] = useState<string>();

  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const queryClient = useQueryClient();

  const drConfigMutation = useMutation(
    (formValues: CreateDrConfigFormValues) => {
      const createDrConfigRequest: CreateDrConfigRequest = {
        name: `dr-config-${generateUniqueName()}`,
        sourceUniverseUUID: sourceUniverseUuid,
        targetUniverseUUID: formValues.targetUniverse.value.universeUUID,
        dbs: formValues.namespaceUuids.map(formatUuidForXCluster),
        bootstrapParams: {
          backupRequestParams: {
            storageConfigUUID: formValues.storageConfig.value.uuid
          }
        }
      };
      return api.createDrConfig(createDrConfigRequest);
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

  const tablesQuery = useQuery<YBTable[]>(
    universeQueryKey.tables(sourceUniverseUuid, XCLUSTER_UNIVERSE_TABLE_FILTERS),
    () =>
      fetchTablesInUniverse(sourceUniverseUuid, XCLUSTER_UNIVERSE_TABLE_FILTERS).then(
        (response) => response.data
      )
  );
  const sourceUniverseQuery = useQuery<Universe>(universeQueryKey.detail(sourceUniverseUuid), () =>
    api.fetchUniverse(sourceUniverseUuid)
  );

  const formMethods = useForm<CreateDrConfigFormValues>({
    defaultValues: {
      namespaceUuids: [],
      tableUuids: []
    }
  });

  const modalTitle = t('title');
  const cancelLabel = t('cancel', { keyPrefix: 'common' });
  if (
    tablesQuery.isLoading ||
    tablesQuery.isIdle ||
    sourceUniverseQuery.isLoading ||
    sourceUniverseQuery.isIdle
  ) {
    return (
      <YBModal
        title={modalTitle}
        cancelLabel={cancelLabel}
        submitTestId={`${MODAL_NAME}-SubmitButton`}
        cancelTestId={`${MODAL_NAME}-CancelButton`}
        maxWidth="xl"
        size="md"
        overrideWidth="960px"
        {...modalProps}
      >
        <YBLoading />
      </YBModal>
    );
  }

  if (tablesQuery.isError || sourceUniverseQuery.isError) {
    return (
      <YBModal
        title={modalTitle}
        cancelLabel={cancelLabel}
        submitTestId={`${MODAL_NAME}-SubmitButton`}
        cancelTestId={`${MODAL_NAME}-CancelButton`}
        maxWidth="xl"
        size="md"
        overrideWidth="960px"
        {...modalProps}
      >
        <YBErrorIndicator
          customErrorMessage={t('failedToFetchSourceUniverse', {
            keyPrefix: 'clusterDetail.xCluster.error',
            universeUuid: sourceUniverseUuid
          })}
        />
      </YBModal>
    );
  }

  /**
   * Reset the selection back to defaults.
   */
  const resetTableSelection = () => {
    // resetField() will also clear errors unless
    // `keepError` option is passed.
    formMethods.resetField('namespaceUuids');
    formMethods.resetField('tableUuids');
    setTableSelectionError(undefined);
  };

  const onSubmit: SubmitHandler<CreateDrConfigFormValues> = async (formValues) => {
    switch (currentFormStep) {
      case FormStep.SELECT_TARGET_UNIVERSE:
        if (formValues.targetUniverse.value.universeUUID !== committedTargetUniverseUUID) {
          // Reset table selection when changing target universe.
          // This is because the current table selection may be invalid for
          // the new target universe.
          resetTableSelection();
          setCommittedTargetUniverseUUID(formValues.targetUniverse.value.universeUUID);
        }
        setCurrentFormStep(FormStep.SELECT_TABLES);
        return;
      case FormStep.SELECT_TABLES:
        // For the create DR user flow, we will always ask for bootstrap params from the user.
        // This means there is no need to check whether the selected tables require bootstrapping in
        // this step.
        if (formValues.namespaceUuids.length <= 0) {
          formMethods.setError('namespaceUuids', {
            type: 'min',
            message: t('error.validationMinimumNamespaceUuids.title', {
              keyPrefix: SELECT_TABLE_TRANSLATION_KEY_PREFIX
            })
          });
          // The TableSelect component expects error objects with title and body fields.
          // React-hook-form only allows string error messages.
          // Thus, we need an store these error objects separately.
          setTableSelectionError({
            title: t('error.validationMinimumNamespaceUuids.title', {
              keyPrefix: SELECT_TABLE_TRANSLATION_KEY_PREFIX
            }),
            body: t('error.validationMinimumNamespaceUuids.body', {
              keyPrefix: SELECT_TABLE_TRANSLATION_KEY_PREFIX
            })
          });
        } else {
          setCurrentFormStep(FormStep.CONFIGURE_BOOTSTRAP);
        }
        return;
      case FormStep.CONFIGURE_BOOTSTRAP:
        setCurrentFormStep(FormStep.CONFIRM_ALERT);
        return;
      case FormStep.CONFIRM_ALERT:
        return drConfigMutation.mutateAsync(formValues);
      default:
        return assertUnreachableCase(currentFormStep);
    }
  };

  const handleBackNavigation = () => {
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
        setCurrentFormStep(FormStep.CONFIGURE_BOOTSTRAP);
        return;
      default:
        assertUnreachableCase(currentFormStep);
    }
  };

  const getFormSubmitLabel = (formStep: FormStep) => {
    switch (formStep) {
      case FormStep.SELECT_TARGET_UNIVERSE:
        return t('step.selectTargetUniverse.submitButton');
      case FormStep.SELECT_TABLES:
        return t('step.selectDatabases.submitButton');
      case FormStep.CONFIGURE_BOOTSTRAP:
        return t('step.configureBootstrap.submitButton');
      case FormStep.CONFIRM_ALERT:
        return t('step.confirmAlert.submitButton');
      default:
        return assertUnreachableCase(formStep);
    }
  };

  const setSelectedNamespaceUuids = (namespaces: string[]) => {
    // Clear any existing errors.
    // The new table/namespace selection will need to be (re)validated.
    setTableSelectionError(undefined);
    formMethods.clearErrors('namespaceUuids');

    // We will run any required validation on selected namespaces & tables all at once when the
    // user clicks on the 'Validate Selection' button.
    formMethods.setValue('namespaceUuids', namespaces, { shouldValidate: false });
  };
  const setSelectedTableUuids = (tableUuids: string[]) => {
    // Clear any existing errors.
    // The new table/namespace selection will need to be (re)validated.
    setTableSelectionError(undefined);
    formMethods.clearErrors('tableUuids');

    // We will run any required validation on selected namespaces & tables all at once when the
    // user clicks on the 'Validate Selection' button.
    formMethods.setValue('tableUuids', tableUuids, { shouldValidate: false });
  };

  const sourceUniverse = sourceUniverseQuery.data;
  const submitLabel = getFormSubmitLabel(currentFormStep);
  const selectedTableUuids = formMethods.watch('tableUuids');
  const selectedNamespaceUuids = formMethods.watch('namespaceUuids');
  const targetUniverseUuid = formMethods.watch('targetUniverse.value.universeUUID');
  const isConfigureActionFrozen =
    currentFormStep === FormStep.CONFIRM_ALERT &&
    isActionFrozen(allowedTasks, UNIVERSE_TASKS.CONFIGURE_DR);
  const isFormDisabled = formMethods.formState.isSubmitting || isConfigureActionFrozen;

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
          sourceUniverse={sourceUniverse}
          tableSelectProps={{
            configAction: XClusterConfigAction.CREATE,
            handleTransactionalConfigCheckboxClick: () => {},
            isDrInterface: true,
            isFixedTableType: false,
            isTransactionalConfig: true,
            initialNamespaceUuids: [],
            selectedNamespaceUuids: selectedNamespaceUuids,
            selectedTableUuids: selectedTableUuids,
            selectionError: tableSelectionError,
            selectionWarning: undefined,
            setSelectedNamespaceUuids: setSelectedNamespaceUuids,
            setSelectedTableUuids: setSelectedTableUuids,
            setTableType: () => {}, // DR is only available for YSQL
            sourceUniverseUuid: sourceUniverseUuid,
            tableType: TableType.PGSQL_TABLE_TYPE,
            targetUniverseUuid: targetUniverseUuid
          }}
        />
      </FormProvider>
    </YBModal>
  );
};

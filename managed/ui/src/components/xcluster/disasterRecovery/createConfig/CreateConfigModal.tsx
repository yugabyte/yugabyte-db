import { useState } from 'react';
import { AxiosError } from 'axios';
import { Box, Typography, useTheme } from '@material-ui/core';
import { toast } from 'react-toastify';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { useTranslation } from 'react-i18next';
import { FormProvider, SubmitHandler, useForm } from 'react-hook-form';

import {
  fetchTablesInUniverse,
  fetchTaskUntilItCompletes,
  isBootstrapRequired
} from '../../../../actions/xClusterReplication';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import {
  formatUuidForXCluster,
  getCategorizedNeedBootstrapPerTableResponse
} from '../../ReplicationUtils';

import { assertUnreachableCase, handleServerError } from '../../../../utils/errorHandlingUtils';
import {
  api,
  CreateDrConfigRequest,
  drConfigQueryKey,
  runtimeConfigQueryKey,
  universeQueryKey
} from '../../../../redesign/helpers/api';
import { YBButton, YBModal, YBModalProps } from '../../../../redesign/components';
import { CurrentFormStep } from './CurrentFormStep';
import { StorageConfigOption } from '../../sharedComponents/ReactSelectStorageConfig';
import {
  XClusterConfigAction,
  XClusterConfigType,
  XCLUSTER_TRANSACTIONAL_PITR_RETENTION_PERIOD_SECONDS_FALLBACK,
  XCLUSTER_UNIVERSE_TABLE_FILTERS
} from '../../constants';
import {
  DurationUnit,
  DURATION_UNIT_TO_SECONDS,
  PITR_RETENTION_PERIOD_UNIT_OPTIONS
} from '../constants';
import { RuntimeConfigKey } from '../../../../redesign/helpers/constants';
import { parseDurationToSeconds } from '../../../../utils/parsers';
import { convertSecondsToLargestDurationUnit } from '../utils';
import { generateUniqueName } from '../../../../redesign/helpers/utils';

import { RunTimeConfigEntry } from '../../../../redesign/features/universe/universe-form/utils/dto';
import { TableType, Universe, YBTable } from '../../../../redesign/helpers/dtos';
import { CategorizedNeedBootstrapPerTableResponse } from '../../XClusterTypes';
import { XClusterConfigNeedBootstrapPerTableResponse } from '../../dtos';

import toastStyles from '../../../../redesign/styles/toastStyles.module.scss';

export interface CreateDrConfigFormValues {
  configName: string;
  targetUniverse: { label: string; value: Universe; isDisabled: boolean; disabledReason?: string };
  namespaceUuids: string[];
  tableUuids: string[];
  storageConfig: StorageConfigOption;
  pitrRetentionPeriodValue: number;
  pitrRetentionPeriodUnit: { label: string; value: DurationUnit };
}

interface CreateConfigModalProps {
  modalProps: YBModalProps;
  sourceUniverseUuid: string;
}

export const FormStep = {
  SELECT_TARGET_UNIVERSE: 'selectTargetUniverse',
  SELECT_TABLES: 'selectDatabases',
  BOOTSTRAP_SUMMARY: 'bootstrapSummary',
  CONFIGURE_PITR: 'configurePitr',
  CONFIRM_ALERT: 'configureAlert'
} as const;
export type FormStep = typeof FormStep[keyof typeof FormStep];

const MODAL_NAME = 'CreateConfigModal';
const FIRST_FORM_STEP = FormStep.SELECT_TARGET_UNIVERSE;
const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.config.createModal';
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
  const [isTableSelectionProcessed, setIsTableSelectionProcessed] = useState<boolean>(false);
  // The purpose of committedTargetUniverseUuid is to store the target universe uuid prior
  // to the user submitting their select target universe step.
  // This value updates whenever the user submits SelectTargetUniverseStep with a new
  // target universe.
  const [committedTargetUniverseUuid, setCommittedTargetUniverseUuid] = useState<string>();

  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const queryClient = useQueryClient();
  const theme = useTheme();

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
  const runtimeConfigQuery = useQuery(runtimeConfigQueryKey.universeScope(sourceUniverseUuid), () =>
    api.fetchRuntimeConfigs(sourceUniverseUuid, true)
  );

  const drConfigMutation = useMutation(
    ({ formValues, isDbScoped }: { formValues: CreateDrConfigFormValues; isDbScoped: boolean }) => {
      const retentionPeriodSec =
        formValues.pitrRetentionPeriodValue *
        DURATION_UNIT_TO_SECONDS[formValues.pitrRetentionPeriodUnit.value];

      const createDrConfigRequest: CreateDrConfigRequest = {
        name: formValues.configName,
        sourceUniverseUUID: sourceUniverseUuid,
        targetUniverseUUID: formValues.targetUniverse.value.universeUUID,
        dbs: formValues.namespaceUuids.map(formatUuidForXCluster),
        bootstrapParams: {
          backupRequestParams: {
            storageConfigUUID: formValues.storageConfig.value.uuid
          }
        },
        pitrParams: {
          retentionPeriodSec: retentionPeriodSec
        },
        dbScoped: isDbScoped
      };
      return api.createDrConfig(createDrConfigRequest);
    },
    {
      onSuccess: async (response, { formValues }) => {
        const invalidateQueries = () => {
          queryClient.invalidateQueries(drConfigQueryKey.detail(response.resourceUUID));
          // The new DR config will update the sourceXClusterConfigs for the source universe and
          // to targetXClusterConfigs for the target universe.
          // Invalidate queries for the participating universes.
          queryClient.invalidateQueries(universeQueryKey.detail(sourceUniverseUuid), {
            exact: true
          });
          queryClient.invalidateQueries(
            universeQueryKey.detail(formValues.targetUniverse.value.universeUUID),
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

  const formMethods = useForm<CreateDrConfigFormValues>({
    defaultValues: runtimeConfigQuery.data
      ? getDefaultValues(runtimeConfigQuery.data?.configEntries ?? [])
      : {}
  });

  const modalTitle = t('title');
  const cancelLabel = t('cancel', { keyPrefix: 'common' });
  if (
    tablesQuery.isLoading ||
    tablesQuery.isIdle ||
    sourceUniverseQuery.isLoading ||
    sourceUniverseQuery.isIdle ||
    runtimeConfigQuery.isLoading ||
    runtimeConfigQuery.isIdle
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

  if (tablesQuery.isError || sourceUniverseQuery.isError || runtimeConfigQuery.isError) {
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

  const setSelectedNamespaceUuids = (namespaces: string[]) => {
    // Clear any existing errors.
    // The new table/namespace selection will need to be (re)validated.
    setTableSelectionError(null);
    formMethods.clearErrors('namespaceUuids');

    // We will run any required validation on selected namespaces & tables all at once when the
    // user clicks on the 'Validate Selection' button.
    formMethods.setValue('namespaceUuids', namespaces, { shouldValidate: false });
  };
  const setSelectedTableUuids = (tableUuids: string[]) => {
    // Clear any existing errors.
    // The new table/namespace selection will need to be (re)validated.
    setTableSelectionError(null);
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
  };

  if (
    formMethods.formState.defaultValues &&
    Object.keys(formMethods.formState.defaultValues).length === 0
  ) {
    // react-hook-form caches the defaultValues on first render.
    // We need to update the defaultValues with reset() after regionMetadataQuery is successful.
    formMethods.reset(getDefaultValues(runtimeConfigQuery.data.configEntries ?? []));
  }

  const runtimeConfigEntries = runtimeConfigQuery.data.configEntries ?? [];
  const isDbScopedEnabled =
    runtimeConfigEntries.find(
      (config: any) => config.key === RuntimeConfigKey.XCLUSTER_DB_SCOPED_CREATION_FEATURE_FLAG
    )?.value ?? false;

  const onSubmit: SubmitHandler<CreateDrConfigFormValues> = async (formValues) => {
    // When the user changes target universe, the old table selection is no longer valid.
    const isTableSelectionInvalidated =
      formValues.targetUniverse.value.universeUUID !== committedTargetUniverseUuid;
    switch (currentFormStep) {
      case FormStep.SELECT_TARGET_UNIVERSE:
        if (isTableSelectionInvalidated) {
          resetTableSelection();
        }
        if (formValues.targetUniverse.value.universeUUID !== committedTargetUniverseUuid) {
          setCommittedTargetUniverseUuid(formValues.targetUniverse.value.universeUUID);
        }
        setCurrentFormStep(FormStep.SELECT_TABLES);
        return;
      case FormStep.SELECT_TABLES:
        if (formValues.namespaceUuids.length <= 0) {
          formMethods.setError('namespaceUuids', {
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
        }
        if (!isTableSelectionProcessed) {
          let xClusterConfigNeedBootstrapPerTableResponse: XClusterConfigNeedBootstrapPerTableResponse = {};
          try {
            xClusterConfigNeedBootstrapPerTableResponse = await isBootstrapRequired(
              sourceUniverseUuid,
              targetUniverseUuid,
              formValues.tableUuids,
              XClusterConfigType.TXN,
              true /* includeDetails */,
              true /* isUsedForDr */
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
          setIsTableSelectionProcessed(true);
        }
        setCurrentFormStep(FormStep.BOOTSTRAP_SUMMARY);
        return;
      case FormStep.BOOTSTRAP_SUMMARY:
        setCurrentFormStep(FormStep.CONFIGURE_PITR);
        return;
      case FormStep.CONFIGURE_PITR:
        setCurrentFormStep(FormStep.CONFIRM_ALERT);
        return;
      case FormStep.CONFIRM_ALERT:
        return drConfigMutation.mutateAsync({
          formValues,
          isDbScoped: isDbScopedEnabled
        });
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
      case FormStep.CONFIGURE_PITR:
        setCurrentFormStep(FormStep.BOOTSTRAP_SUMMARY);
        return;
      case FormStep.CONFIRM_ALERT:
        setCurrentFormStep(FormStep.CONFIGURE_PITR);
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
      case FormStep.BOOTSTRAP_SUMMARY:
        return t('step.bootstrapSummary.submitButton');
      case FormStep.CONFIGURE_PITR:
        return t('step.configurePitr.submitButton');
      case FormStep.CONFIRM_ALERT:
        return t('step.confirmAlert.submitButton');
      default:
        return assertUnreachableCase(formStep);
    }
  };

  const xClusterConfigType = isDbScopedEnabled
    ? XClusterConfigType.DB_SCOPED
    : XClusterConfigType.TXN;
  const sourceUniverse = sourceUniverseQuery.data;
  const submitLabel = getFormSubmitLabel(currentFormStep);
  const selectedTableUuids = formMethods.watch('tableUuids');
  const selectedNamespaceUuids = formMethods.watch('namespaceUuids');
  const targetUniverseUuid = formMethods.watch('targetUniverse.value.universeUUID');
  const isFormDisabled = formMethods.formState.isSubmitting;

  return (
    <YBModal
      title={modalTitle}
      submitLabel={submitLabel}
      buttonProps={{ primary: { disabled: isFormDisabled } }}
      onSubmit={formMethods.handleSubmit(onSubmit)}
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      isSubmitting={formMethods.formState.isSubmitting}
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
            isDrInterface: true,
            initialNamespaceUuids: [],
            selectedNamespaceUuids: selectedNamespaceUuids,
            selectedTableUuids: selectedTableUuids,
            selectionError: tableSelectionError,
            selectionWarning: tableSelectionWarning,
            setSelectedNamespaceUuids: setSelectedNamespaceUuids,
            setSelectedTableUuids: setSelectedTableUuids,
            sourceUniverseUuid: sourceUniverseUuid,
            tableType: TableType.PGSQL_TABLE_TYPE,
            xClusterConfigType: xClusterConfigType,
            targetUniverseUuid: targetUniverseUuid
          }}
          categorizedNeedBootstrapPerTableResponse={categorizedNeedBootstrapPerTableResponse}
        />
      </FormProvider>
    </YBModal>
  );
};

const getDefaultValues = (runtimeConfigEntries: RunTimeConfigEntry[]) => {
  const runtimeConfigDefaultPitrRetentionPeriodSeconds = parseDurationToSeconds(
    runtimeConfigEntries.find(
      (config: any) => config.key === RuntimeConfigKey.XCLUSTER_TRANSACTIONAL_PITR_RETENTION_PERIOD
    )?.value ?? '',
    { noThrow: true }
  );
  const defaultPitrRetentionPeriodSeconds =
    isNaN(runtimeConfigDefaultPitrRetentionPeriodSeconds) ||
    runtimeConfigDefaultPitrRetentionPeriodSeconds < 0
      ? XCLUSTER_TRANSACTIONAL_PITR_RETENTION_PERIOD_SECONDS_FALLBACK
      : runtimeConfigDefaultPitrRetentionPeriodSeconds;
  const {
    value: pitrRetentionPeriodValue,
    unit: durationUnit
  } = convertSecondsToLargestDurationUnit(defaultPitrRetentionPeriodSeconds, { noThrow: true });
  const pitrRetentionPeriodUnit = PITR_RETENTION_PERIOD_UNIT_OPTIONS.find(
    (option) => option.value === durationUnit
  );

  return {
    configName: `dr-config-${generateUniqueName()}`,
    namespaceUuids: [],
    tableUuids: [],
    // Fall back to hours if we can't find a matching duration unit.
    pitrRetentionPeriodValue: pitrRetentionPeriodUnit
      ? pitrRetentionPeriodValue
      : Math.max(1, Math.round(defaultPitrRetentionPeriodSeconds / 3600)),
    pitrRetentionPeriodUnit: pitrRetentionPeriodUnit
      ? pitrRetentionPeriodUnit
      : PITR_RETENTION_PERIOD_UNIT_OPTIONS.find((option) => option.value === DurationUnit.HOUR)
  };
};

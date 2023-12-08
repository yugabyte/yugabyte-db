import { useRef, useState } from 'react';
import { AxiosError } from 'axios';
import { FormikActions, FormikErrors, FormikProps } from 'formik';
import { makeStyles, Typography } from '@material-ui/core';
import { toast } from 'react-toastify';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { useTranslation } from 'react-i18next';

import {
  fetchTablesInUniverse,
  fetchTaskUntilItCompletes
} from '../../../../actions/xClusterReplication';
import { YBModalForm } from '../../../common/forms';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import { formatUuidForXCluster } from '../../ReplicationUtils';
import { XClusterConfigAction } from '../../constants';
import { assertUnreachableCase, handleServerError } from '../../../../utils/errorHandlingUtils';
import {
  api,
  CreateDrConfigRequest,
  drConfigQueryKey,
  universeQueryKey
} from '../../../../redesign/helpers/api';
import { YBButton, YBModal } from '../../../common/forms/fields';
import { TableSelect } from '../../sharedComponents/tableSelect/TableSelect';
import { getPrimaryCluster } from '../../../../utils/universeUtilsTyped';
import { SelectTargetUniverseStep } from './SelectTargetUniverseStep';
import { ConfigureBootstrapStep } from './ConfigureBootstrapStep';
import { generateUniqueName } from '../../../../redesign/helpers/utils';

import { TableType, Universe, YBTable } from '../../../../redesign/helpers/dtos';

import toastStyles from '../../../../redesign/styles/toastStyles.module.scss';

export interface CreateDrConfigFormValues {
  targetUniverse: { label: string; value: Universe };
  tableUUIDs: string[];
  // Bootstrap configuration fields
  storageConfig: { label: string; name: string; regions: any[]; value: string };
}

export interface CreateDrConfigFormErrors {
  targetUniverse: string;
  tableUUIDs: { title: string; body: string };
  // Bootstrap configuration fields
  storageConfig: string;
}

export interface CreateXClusterConfigFormWarnings {
  targetUniverse?: string;
  tableUUIDs?: { title: string; body: string };
  // Bootstrap configuration fields
  storageConfig?: string;
}

interface CreateConfigModalProps {
  onHide: Function;
  visible: boolean;
  sourceUniverseUuid: string;
}
const useStyles = makeStyles((theme) => ({
  formInstruction: {
    marginBottom: theme.spacing(3)
  }
}));

export const FormStep = {
  SELECT_TARGET_UNIVERSE: 'selectTargetUniverse',
  SELECT_TABLES: 'selectDatabases',
  CONFIGURE_BOOTSTRAP: 'configureBootstrap'
} as const;
export type FormStep = typeof FormStep[keyof typeof FormStep];

const FIRST_FORM_STEP = FormStep.SELECT_TARGET_UNIVERSE;
const MODAL_TITLE = 'Configure Active-Active Single Master Disaster Recovery (DR)';
const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.config.createModal';

export const CreateConfigModal = ({
  onHide,
  visible,
  sourceUniverseUuid
}: CreateConfigModalProps) => {
  const [currentStep, setCurrentStep] = useState<FormStep>(FIRST_FORM_STEP);
  const [isTableSelectionValidated, setIsTableSelectionValidated] = useState(false);
  const [formWarnings, setFormWarnings] = useState<CreateXClusterConfigFormWarnings>({});

  // The purpose of committedTargetUniverse is to store the targetUniverse field value prior
  // to the user submitting their target universe step.
  // This value updates whenever the user submits SelectTargetUniverseStep with a new
  // target universe.
  const [committedTargetUniverseUUID, setCommittedTargetUniverseUUID] = useState<string>();

  const [selectedKeyspaces, setSelectedKeyspaces] = useState<string[]>([]);

  const formik = useRef({} as FormikProps<CreateDrConfigFormValues>);
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const queryClient = useQueryClient();
  const classes = useStyles();

  const drConfigMutation = useMutation(
    (formValues: CreateDrConfigFormValues) => {
      const createDrConfigRequest: CreateDrConfigRequest = {
        name: `dr-config-${generateUniqueName()}`,
        sourceUniverseUUID: sourceUniverseUuid,
        targetUniverseUUID: formValues.targetUniverse.value.universeUUID,
        dbs: selectedKeyspaces.map(formatUuidForXCluster),
        bootstrapBackupParams: {
          storageConfigUUID: formValues.storageConfig.value
        }
      };
      return api.createDrConfig(createDrConfigRequest);
    },
    {
      onSuccess: (response, values) => {
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
        closeModal();
        fetchTaskUntilItCompletes(response.taskUUID, handleTaskCompletion, invalidateQueries);
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, { customErrorLabel: t('error.request') })
    }
  );

  const tablesQuery = useQuery<YBTable[]>(
    universeQueryKey.tables(sourceUniverseUuid, {
      excludeColocatedTables: true,
      xClusterSupportedOnly: true
    }),
    () =>
      fetchTablesInUniverse(sourceUniverseUuid, {
        excludeColocatedTables: true,
        xClusterSupportedOnly: true
      }).then((response) => response.data)
  );
  const universeQuery = useQuery<Universe>(universeQueryKey.detail(sourceUniverseUuid), () =>
    api.fetchUniverse(sourceUniverseUuid)
  );

  /**
   * Wrapper around setFieldValue from formik.
   * Reset `isTableSelectionValidated` to false if changing
   * a validated table selection.
   */
  const setSelectedTableUUIDs = (
    tableUUIDs: string[],
    formikActions: FormikActions<CreateDrConfigFormValues>
  ) => {
    if (isTableSelectionValidated) {
      // We need to validate the new selection.
      setIsTableSelectionValidated(false);
    }
    formikActions.setFieldValue('tableUUIDs', tableUUIDs);
  };

  const resetTableSelection = (formikActions: FormikActions<CreateDrConfigFormValues>) => {
    setSelectedTableUUIDs([], formikActions);
    setSelectedKeyspaces([]);
    setFormWarnings((formWarnings) => {
      const { tableUUIDs, ...newformWarnings } = formWarnings;
      return newformWarnings;
    });
  };
  const resetModalState = () => {
    setCurrentStep(FIRST_FORM_STEP);
    setIsTableSelectionValidated(false);
    setFormWarnings({});
    setSelectedKeyspaces([]);
  };
  const closeModal = () => {
    resetModalState();
    onHide();
  };

  const handleFormSubmit = (
    values: CreateDrConfigFormValues,
    actions: FormikActions<CreateDrConfigFormValues>
  ) => {
    switch (currentStep) {
      case FormStep.SELECT_TARGET_UNIVERSE:
        if (values.targetUniverse.value.universeUUID !== committedTargetUniverseUUID) {
          // Reset table selection when changing target universe.
          // This is because the current table selection may be invalid for
          // the new target universe.
          resetTableSelection(actions);
          setCommittedTargetUniverseUUID(values.targetUniverse.value.universeUUID);
        }
        setCurrentStep(FormStep.SELECT_TABLES);
        actions.setSubmitting(false);
        return;
      case FormStep.SELECT_TABLES: {
        if (!isTableSelectionValidated) {
          // Validation in validateForm just passed.
          setIsTableSelectionValidated(true);
          actions.setSubmitting(false);
          return;
        }

        // Table selection has already been validated.
        setCurrentStep(FormStep.CONFIGURE_BOOTSTRAP);
        actions.setSubmitting(false);
        return;
      }
      case FormStep.CONFIGURE_BOOTSTRAP:
        drConfigMutation.mutate(values, { onSettled: () => actions.setSubmitting(false) });
        return;
      default:
        assertUnreachableCase(currentStep);
    }
  };

  const handleBackNavigation = (currentStep: Exclude<FormStep, typeof FIRST_FORM_STEP>) => {
    switch (currentStep) {
      case FormStep.SELECT_TABLES:
        setCurrentStep(FormStep.SELECT_TARGET_UNIVERSE);
        return;
      case FormStep.CONFIGURE_BOOTSTRAP:
        setCurrentStep(FormStep.SELECT_TABLES);
        return;
      default:
        assertUnreachableCase(currentStep);
    }
  };

  const getFormSubmitLabel = (formStep: FormStep, validTableSelection: boolean) => {
    switch (formStep) {
      case FormStep.SELECT_TARGET_UNIVERSE:
        return t('step.selectTargetUniverse.submitButton');
      case FormStep.SELECT_TABLES:
        if (!validTableSelection) {
          return 'Validate Table Selection';
        }
        return t('step.selectDatabases.submitButton');
      case FormStep.CONFIGURE_BOOTSTRAP:
        return t('step.configureBootstrap.submitButton');
      default:
        return assertUnreachableCase(formStep);
    }
  };

  const submitLabel = getFormSubmitLabel(currentStep, isTableSelectionValidated);
  if (
    tablesQuery.isLoading ||
    tablesQuery.isIdle ||
    universeQuery.isLoading ||
    universeQuery.isIdle
  ) {
    return (
      <YBModal
        size="large"
        title={MODAL_TITLE}
        visible={visible}
        onHide={() => {
          closeModal();
        }}
        submitLabel={submitLabel}
      >
        <YBLoading />
      </YBModal>
    );
  }

  if (tablesQuery.isError || universeQuery.isError) {
    return (
      <YBModal
        size="large"
        title={MODAL_TITLE}
        visible={visible}
        onHide={() => {
          closeModal();
        }}
      >
        <YBErrorIndicator customErrorMessage="Encounter an error fetching information for tables from the source universe." />
      </YBModal>
    );
  }

  const INITIAL_VALUES: Partial<CreateDrConfigFormValues> = {
    tableUUIDs: []
  };
  return (
    <YBModalForm
      size="large"
      title={MODAL_TITLE}
      visible={visible}
      validate={(values: CreateDrConfigFormValues) =>
        validateForm(
          values,
          currentStep,
          universeQuery.data,
          isTableSelectionValidated,
          setFormWarnings
        )
      }
      // Perform validation for select table only when user submits.
      validateOnChange={currentStep !== FormStep.SELECT_TABLES}
      validateOnBlur={currentStep !== FormStep.SELECT_TABLES}
      onFormSubmit={handleFormSubmit}
      initialValues={INITIAL_VALUES}
      submitLabel={submitLabel}
      onHide={() => {
        closeModal();
      }}
      footerAccessory={
        currentStep === FIRST_FORM_STEP ? (
          <YBButton
            btnClass="btn"
            btnText={t('cancel', { keyPrefix: 'common' })}
            onClick={closeModal}
          />
        ) : (
          <YBButton
            btnClass="btn"
            btnText={t('back', { keyPrefix: 'common' })}
            onClick={() => handleBackNavigation(currentStep)}
          />
        )
      }
      render={(formikProps: FormikProps<CreateDrConfigFormValues>) => {
        // workaround for outdated version of Formik to access form methods outside of <Formik>
        formik.current = formikProps;

        if (
          tablesQuery.isLoading ||
          tablesQuery.isIdle ||
          universeQuery.isLoading ||
          universeQuery.isIdle
        ) {
          return <YBLoading />;
        }

        if (tablesQuery.isError || universeQuery.isError) {
          return <YBErrorIndicator />;
        }

        switch (currentStep) {
          case FormStep.SELECT_TARGET_UNIVERSE:
            return (
              <SelectTargetUniverseStep formik={formik} currentUniverseUuid={sourceUniverseUuid} />
            );
          case FormStep.SELECT_TABLES: {
            // Casting because FormikValues and FormikError have different types.
            const errors = formik.current.errors as FormikErrors<CreateDrConfigFormErrors>;
            const { values } = formik.current;
            return (
              <>
                <div className={classes.formInstruction}>
                  {t('step.selectDatabases.instruction')}
                </div>
                <TableSelect
                  {...{
                    configAction: XClusterConfigAction.CREATE,
                    handleTransactionalConfigCheckboxClick: () => {},
                    isDrInterface: true,
                    isFixedTableType: false,
                    isTransactionalConfig: true,
                    initialNamespaceUuids: [],
                    selectedNamespaceUuids: selectedKeyspaces,
                    selectedTableUUIDs: values.tableUUIDs,
                    selectionError: errors.tableUUIDs,
                    selectionWarning: formWarnings?.tableUUIDs,
                    setSelectedNamespaceUuids: setSelectedKeyspaces,
                    setSelectedTableUUIDs: (tableUUIDs: string[]) =>
                      setSelectedTableUUIDs(tableUUIDs, formik.current),
                    setTableType: () => {},
                    sourceUniverseUUID: sourceUniverseUuid,
                    tableType: TableType.PGSQL_TABLE_TYPE,
                    targetUniverseUUID: values.targetUniverse.value.universeUUID
                  }}
                />
              </>
            );
          }
          case FormStep.CONFIGURE_BOOTSTRAP:
            return <ConfigureBootstrapStep formik={formik} />;
          default:
            return assertUnreachableCase(currentStep);
        }
      }}
    />
  );
};

const validateForm = async (
  values: CreateDrConfigFormValues,
  currentStep: FormStep,
  sourceUniverse: Universe,
  isTableSelectionValidated: boolean,
  setFormWarnings: (formWarnings: CreateXClusterConfigFormWarnings) => void
) => {
  // Since our formik verision is < 2.0 , we need to throw errors instead of
  // returning them in custom async validation:
  // https://github.com/jaredpalmer/formik/issues/1392#issuecomment-606301031

  switch (currentStep) {
    case FormStep.SELECT_TARGET_UNIVERSE: {
      const errors: Partial<CreateDrConfigFormErrors> = {};

      if (!values.targetUniverse) {
        errors.targetUniverse = 'DR replica universe is required.';
      } else if (
        getPrimaryCluster(values.targetUniverse.value.universeDetails.clusters)?.userIntent
          ?.enableNodeToNodeEncrypt !==
        getPrimaryCluster(sourceUniverse?.universeDetails.clusters)?.userIntent
          ?.enableNodeToNodeEncrypt
      ) {
        errors.targetUniverse =
          'The DR replica must have the same Encryption in-Transit (TLS) configuration as the source universe. Edit the TLS configuration to proceed.';
      }

      throw errors;
    }
    case FormStep.SELECT_TABLES: {
      const errors: Partial<CreateDrConfigFormErrors> = {};
      const warnings: CreateXClusterConfigFormWarnings = {};
      if (!isTableSelectionValidated) {
        if (!values.tableUUIDs || values.tableUUIDs.length === 0) {
          errors.tableUUIDs = {
            title: 'No databases selected.',
            body: 'Select at least 1 database to proceed'
          };
        }
        setFormWarnings(warnings);
      }
      throw errors;
    }
    case FormStep.CONFIGURE_BOOTSTRAP: {
      const errors: Partial<CreateDrConfigFormErrors> = {};
      if (!values.storageConfig) {
        errors.storageConfig = 'Backup storage configuration is required.';
      }
      throw errors;
    }
    default:
      throw {};
  }
};

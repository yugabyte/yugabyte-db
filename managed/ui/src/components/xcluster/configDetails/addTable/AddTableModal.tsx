import { FormikActions, FormikErrors, FormikProps } from 'formik';
import { useRef, useState } from 'react';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import { AxiosError } from 'axios';

import { api, universeQueryKey, xClusterQueryKey } from '../../../../redesign/helpers/api';
import { TableTypeLabel, Universe, YBTable } from '../../../../redesign/helpers/dtos';
import { assertUnreachableCase, handleServerError } from '../../../../utils/errorHandlingUtils';
import { YBButton, YBModal } from '../../../common/forms/fields';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import { isYbcEnabledUniverse } from '../../../../utils/UniverseUtils';
import { ParallelThreads } from '../../../backupv2/common/BackupUtils';
import { YBModalForm } from '../../../common/forms';
import { ConfigureBootstrapStep } from './ConfigureBootstrapStep';
import {
  BOOTSTRAP_MIN_FREE_DISK_SPACE_GB,
  XClusterConfigAction,
  XClusterConfigType
} from '../../constants';
import {
  getTablesForBootstrapping,
  getXClusterConfigTableType,
  parseFloatIfDefined
} from '../../ReplicationUtils';
import {
  editXClusterConfigTables,
  fetchUniverseDiskUsageMetric,
  fetchTaskUntilItCompletes,
  fetchTablesInUniverse
} from '../../../../actions/xClusterReplication';
import { TableSelect } from '../../sharedComponents/tableSelect/TableSelect';
import { XClusterConfig } from '../../dtos';

import styles from './AddTableModal.module.scss';

export interface AddTableFormValues {
  tableUUIDs: string[];
  // Bootstrap fields
  storageConfig: { label: string; name: string; regions: any[]; value: string };
  parallelThreads: number;
}

export interface AddTableFormErrors {
  tableUUIDs: { title: string; body: string };
  // Bootstrap fields
  storageConfig: string;
  parallelThreads: string;
}

export interface AddTableFormWarnings {
  tableUUIDs?: { title: string; body: string };
  // Bootstrap fields
  storageConfig?: string;
  parallelThreads?: string;
}

interface AddTableModalProps {
  isVisible: boolean;
  onHide: () => void;
  xClusterConfig: XClusterConfig;

  isDrConfig?: boolean;
}

const MODAL_TITLE = 'Add Tables to Replication';

export const FormStep = {
  SELECT_TABLES: 'selectTables',
  CONFIGURE_BOOTSTRAP: 'configureBootstrap'
} as const;
// eslint-disable-next-line no-redeclare
export type FormStep = typeof FormStep[keyof typeof FormStep];

const FIRST_FORM_STEP = FormStep.SELECT_TABLES;

const INITIAL_VALUES: Partial<AddTableFormValues> = {
  tableUUIDs: [],
  // Bootstrap fields
  parallelThreads: ParallelThreads.XCLUSTER_DEFAULT
};

/**
 * Form for adding tables to an existing xCluster configuration.
 */
export const AddTableModal = ({
  isVisible,
  onHide,
  xClusterConfig,
  isDrConfig = false
}: AddTableModalProps) => {
  const [currentStep, setCurrentStep] = useState<FormStep>(FIRST_FORM_STEP);
  const [formWarnings, setFormWarnings] = useState<AddTableFormWarnings>();
  const [bootstrapRequiredTableUUIDs, setBootstrapRequiredTableUUIDs] = useState<string[]>([]);
  const [isTableSelectionValidated, setIsTableSelectionValidated] = useState(false);

  // Need to store this to support navigating between pages
  const [selectedKeyspaces, setSelectedKeyspaces] = useState<string[]>([]);

  const queryClient = useQueryClient();
  const formik = useRef({} as FormikProps<AddTableFormValues>);

  const sourceUniverseQuery = useQuery<Universe>(
    universeQueryKey.detail(xClusterConfig.sourceUniverseUUID),
    () => api.fetchUniverse(xClusterConfig.sourceUniverseUUID)
  );

  const sourceUniverseTablesQuery = useQuery<YBTable[]>(
    universeQueryKey.tables(xClusterConfig.sourceUniverseUUID, {
      excludeColocatedTables: true,
      xClusterSupportedOnly: true
    }),
    () =>
      fetchTablesInUniverse(xClusterConfig.sourceUniverseUUID, {
        excludeColocatedTables: true,
        xClusterSupportedOnly: true
      }).then((response) => response.data)
  );

  const configTablesMutation = useMutation(
    (formValues: AddTableFormValues) => {
      const bootstrapParams =
        bootstrapRequiredTableUUIDs.length > 0
          ? {
              tables: bootstrapRequiredTableUUIDs,
              backupRequestParams: {
                storageConfigUUID: formValues.storageConfig.value,
                parallelism: formValues.parallelThreads,
                sse: formValues.storageConfig.name === 'S3',
                universeUUID: null
              }
            }
          : undefined;
      const tableUUIDs = Array.from(new Set(formValues.tableUUIDs.concat(xClusterConfig.tables)));
      return editXClusterConfigTables(xClusterConfig.uuid, tableUUIDs, bootstrapParams);
    },
    {
      onSuccess: (response) => {
        closeModal();

        fetchTaskUntilItCompletes(
          response.taskUUID,
          (err: boolean) => {
            if (err) {
              toast.error(
                <span className={styles.alertMsg}>
                  <i className="fa fa-exclamation-circle" />
                  <span>{`Add table to xCluster config failed: ${xClusterConfig.name}`}</span>
                  <a href={`/tasks/${response.taskUUID}`} rel="noopener noreferrer" target="_blank">
                    View Details
                  </a>
                </span>
              );
            }
            queryClient.invalidateQueries(xClusterQueryKey.detail(xClusterConfig.uuid));
          },
          // Invalidate the cached data for current xCluster config. The xCluster config status should change to
          // 'in progress' once the task starts.
          () => {
            queryClient.invalidateQueries(xClusterQueryKey.detail(xClusterConfig.uuid));
          }
        );
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, { customErrorLabel: 'Create xCluster config request failed' })
    }
  );

  /**
   * Wrapper around setFieldValue from formik.
   * Reset `isTableSelectionValidated` to false if changing
   * a validated table selection.
   */
  const setSelectedTableUUIDs = (
    tableUUIDs: string[],
    formikActions: FormikActions<AddTableFormValues>
  ) => {
    if (isTableSelectionValidated) {
      setIsTableSelectionValidated(false);
    }
    formikActions.setFieldValue('tableUUIDs', tableUUIDs);
  };

  const resetModalState = () => {
    setCurrentStep(FIRST_FORM_STEP);
    setBootstrapRequiredTableUUIDs([]);
    setIsTableSelectionValidated(false);
    setFormWarnings({});
    setSelectedKeyspaces([]);
  };
  const closeModal = () => {
    resetModalState();
    onHide();
  };

  const isBootstrapStepRequired = bootstrapRequiredTableUUIDs.length > 0;
  const handleFormSubmit = (
    values: AddTableFormValues,
    actions: FormikActions<AddTableFormValues>
  ) => {
    switch (currentStep) {
      case FormStep.SELECT_TABLES:
        if (!isTableSelectionValidated) {
          // Validation in validateForm just passed.
          setIsTableSelectionValidated(true);
          actions.setSubmitting(false);
          return;
        }

        // Table selection has already been validated.
        if (isBootstrapStepRequired) {
          setCurrentStep(FormStep.CONFIGURE_BOOTSTRAP);
          actions.setSubmitting(false);
          return;
        }
        configTablesMutation.mutate(values, { onSettled: () => actions.setSubmitting(false) });
        return;
      case FormStep.CONFIGURE_BOOTSTRAP:
        configTablesMutation.mutate(values, { onSettled: () => actions.setSubmitting(false) });
        return;
      default:
        assertUnreachableCase(currentStep);
    }
  };

  const submitLabel = getFormSubmitLabel(
    currentStep,
    isBootstrapStepRequired,
    isTableSelectionValidated
  );
  if (
    sourceUniverseQuery.isLoading ||
    sourceUniverseQuery.isIdle ||
    sourceUniverseTablesQuery.isLoading ||
    sourceUniverseTablesQuery.isIdle
  ) {
    return (
      <YBModal
        size="large"
        title={MODAL_TITLE}
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

  const xClusterConfigTableType = getXClusterConfigTableType(xClusterConfig);
  if (
    sourceUniverseQuery.isError ||
    sourceUniverseTablesQuery.isError ||
    !xClusterConfigTableType
  ) {
    return (
      <YBModal
        size="large"
        title={MODAL_TITLE}
        visible={isVisible}
        onHide={() => {
          closeModal();
        }}
      >
        <YBErrorIndicator />
      </YBModal>
    );
  }

  const sourceUniverseTables = sourceUniverseTablesQuery.data;
  const sourceUniverse = sourceUniverseQuery.data;
  return (
    <YBModalForm
      size="large"
      title={MODAL_TITLE}
      visible={isVisible}
      validate={(values: AddTableFormValues) =>
        validateForm(
          values,
          currentStep,
          sourceUniverse,
          sourceUniverseTables,
          isTableSelectionValidated,
          xClusterConfig.type,
          setBootstrapRequiredTableUUIDs,
          setFormWarnings
        )
      }
      validateOnChange={currentStep !== FormStep.SELECT_TABLES}
      validateOnBlur={currentStep !== FormStep.SELECT_TABLES}
      onFormSubmit={handleFormSubmit}
      initialValues={INITIAL_VALUES}
      submitLabel={submitLabel}
      onHide={() => {
        closeModal();
      }}
      footerAccessory={<YBButton btnClass="btn" btnText={'Cancel'} onClick={closeModal} />}
      render={(formikProps: FormikProps<AddTableFormValues>) => {
        if (!xClusterConfig.sourceUniverseUUID || !xClusterConfig.targetUniverseUUID) {
          // We can't show anything meaningful if either source or target universe UUID is not a non-empty string.
          return null;
        }
        // workaround for outdated version of Formik to access form methods outside of <Formik>
        formik.current = formikProps;

        switch (currentStep) {
          case FormStep.SELECT_TABLES: {
            // Casting because FormikValues and FormikError have different types.
            const errors = formik.current.errors as FormikErrors<AddTableFormErrors>;
            return (
              <>
                <div className={styles.formInstruction}>
                  {`1. Select the ${TableTypeLabel[xClusterConfigTableType]} tables you want to add to the xCluster configuration.`}
                </div>
                <TableSelect
                  {...{
                    configAction: XClusterConfigAction.ADD_TABLE,
                    isDrConfig,
                    isFixedTableType: true,
                    selectedKeyspaces,
                    selectedTableUUIDs: formik.current.values.tableUUIDs,
                    selectionError: errors.tableUUIDs,
                    selectionWarning: formWarnings?.tableUUIDs,
                    setSelectedKeyspaces,
                    setSelectedTableUUIDs: (tableUUIDs: string[]) =>
                      setSelectedTableUUIDs(tableUUIDs, formik.current),
                    setTableType: (_) => null,
                    sourceUniverseUUID: xClusterConfig.sourceUniverseUUID,
                    tableType: xClusterConfigTableType,
                    targetUniverseUUID: xClusterConfig.targetUniverseUUID,
                    xClusterConfigUUID: xClusterConfig.uuid
                  }}
                />
              </>
            );
          }
          case FormStep.CONFIGURE_BOOTSTRAP: {
            return (
              <>
                <div className={styles.formInstruction}>2. Configure bootstrap</div>
                <ConfigureBootstrapStep formik={formik} />
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
  values: AddTableFormValues,
  currentStep: FormStep,
  sourceUniverse: Universe,
  sourceUniverseTables: YBTable[],
  isTableSelectionValidated: boolean,
  xClusterConfigType: XClusterConfigType,
  setBootstrapRequiredTableUUIDs: (tableUUIDs: string[]) => void,
  setFormWarning: (formWarnings: AddTableFormWarnings) => void
) => {
  // Since our formik verision is < 2.0 , we need to throw errors instead of
  // returning them in custom async validation:
  // https://github.com/jaredpalmer/formik/issues/1392#issuecomment-606301031

  // We will add a `Select table` step shortly after the backend support lands.
  switch (currentStep) {
    case FormStep.SELECT_TABLES: {
      const errors: Partial<AddTableFormErrors> = {};
      const warning: AddTableFormWarnings = {};
      if (isTableSelectionValidated) {
        throw errors;
      }
      if (!values.tableUUIDs || values.tableUUIDs.length === 0) {
        errors.tableUUIDs = {
          title: 'No tables selected.',
          body: 'Select at least 1 table to proceed'
        };
      }
      let bootstrapTableUUIDs: string[] | null = null;
      try {
        // We pass null as the target universe in the following method because add table does not
        // support the case where a matching table does not exist on the target universe.
        bootstrapTableUUIDs = await getTablesForBootstrapping(
          values.tableUUIDs,
          sourceUniverse.universeUUID,
          null /* targetUniverseUUID */,
          sourceUniverseTables,
          xClusterConfigType
        );
      } catch (error: any) {
        toast.error(
          <span className={styles.alertMsg}>
            <div>
              <i className="fa fa-exclamation-circle" />
              <span>Table bootstrap verification failed.</span>
            </div>
            <div>
              An error occured while verifying whether the selected tables require bootstrapping:
            </div>
            <div>{error.message}</div>
          </span>
        );
        errors.tableUUIDs = {
          title: 'Table bootstrap verification error',
          body:
            'An error occured while verifying whether the selected tables require bootstrapping.'
        };
      }
      if (bootstrapTableUUIDs !== null) {
        setBootstrapRequiredTableUUIDs(bootstrapTableUUIDs);

        // If some tables require bootstrapping, we need to validate the source universe has enough
        // disk space.
        if (bootstrapTableUUIDs.length > 0) {
          // Disk space validation
          const currentUniverseNodePrefix = sourceUniverse.universeDetails.nodePrefix;
          const diskUsageMetric = await fetchUniverseDiskUsageMetric(currentUniverseNodePrefix);
          const freeSpaceTrace = diskUsageMetric.disk_usage.data.find(
            (trace) => trace.name === 'free'
          );
          const freeDiskSpace = parseFloatIfDefined(freeSpaceTrace?.y[freeSpaceTrace.y.length - 1]);

          if (freeDiskSpace !== undefined && freeDiskSpace < BOOTSTRAP_MIN_FREE_DISK_SPACE_GB) {
            warning.tableUUIDs = {
              title: 'Insufficient disk space.',
              body: `Some selected tables require bootstrapping. We recommend having at least ${BOOTSTRAP_MIN_FREE_DISK_SPACE_GB} GB of free disk space in the source universe.`
            };
          }
        }
      }

      setFormWarning(warning);
      throw errors;
    }
    case FormStep.CONFIGURE_BOOTSTRAP: {
      const errors: Partial<AddTableFormErrors> = {};
      if (!values.storageConfig) {
        errors.storageConfig = 'Backup storage configuration is required.';
      }
      const shouldValidateParallelThread =
        values.parallelThreads && isYbcEnabledUniverse(sourceUniverse?.universeDetails);
      if (shouldValidateParallelThread && values.parallelThreads > ParallelThreads.MAX) {
        errors.parallelThreads = `Parallel threads must be less than or equal to ${ParallelThreads.MAX}`;
      } else if (shouldValidateParallelThread && values.parallelThreads < ParallelThreads.MIN) {
        errors.parallelThreads = `Parallel threads must be greater than or equal to ${ParallelThreads.MIN}`;
      }

      throw errors;
    }
    default:
      return {};
  }
};

const getFormSubmitLabel = (
  formStep: FormStep,
  bootstrapRequired: boolean,
  validTableSelection: boolean
) => {
  switch (formStep) {
    case FormStep.SELECT_TABLES:
      if (!validTableSelection) {
        return 'Validate Table Selection';
      }
      if (bootstrapRequired) {
        return 'Next: Configure Bootstrap';
      }
      return 'Enable Replication';
    case FormStep.CONFIGURE_BOOTSTRAP:
      return 'Bootstrap and Enable Replication';
    default:
      return assertUnreachableCase(formStep);
  }
};

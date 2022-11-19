import { FormikActions, FormikErrors, FormikProps } from 'formik';
import React, { useRef, useState } from 'react';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';

import { api } from '../../../../redesign/helpers/api';
import { TableType, TableTypeLabel, Universe, YBTable } from '../../../../redesign/helpers/dtos';
import { assertUnreachableCase } from '../../../../utils/ErrorUtils';
import { YBButton, YBModal } from '../../../common/forms/fields';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import { isYbcEnabledUniverse } from '../../../../utils/UniverseUtils';
import { PARALLEL_THREADS_RANGE } from '../../../backupv2/common/BackupUtils';
import { YBModalForm } from '../../../common/forms';
import { ConfigureBootstrapStep } from './ConfigureBootstrapStep';
import { BOOTSTRAP_MIN_FREE_DISK_SPACE_GB, XClusterConfigAction } from '../../constants';
import { adaptTableUUID, parseFloatIfDefined } from '../../ReplicationUtils';
import {
  editXClusterConfigTables,
  fetchUniverseDiskUsageMetric,
  fetchTaskUntilItCompletes,
  isBootstrapRequired,
  fetchTablesInUniverse
} from '../../../../actions/xClusterReplication';
import { TableSelect } from '../../common/tableSelect/TableSelect';
import { YBTableRelationType } from '../../../../redesign/helpers/constants';

import { XClusterConfig, XClusterTableType } from '../..';

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
  configTableType: XClusterTableType;
}

const MODAL_TITLE = 'Add Tables to Replication';

export const FormStep = {
  SELECT_TABLES: 'selectTables',
  CONFIGURE_BOOTSTRAP: 'configureBootstrap'
} as const;
export type FormStep = typeof FormStep[keyof typeof FormStep];

const FIRST_FORM_STEP = FormStep.SELECT_TABLES;

const INITIAL_VALUES: Partial<AddTableFormValues> = {
  tableUUIDs: [],
  // Bootstrap fields
  parallelThreads: PARALLEL_THREADS_RANGE.MIN
};

/**
 * Form for adding tables to an existing xCluster configuration.
 */
export const AddTableModal = ({
  isVisible,
  onHide,
  xClusterConfig,
  configTableType
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
    ['universe', xClusterConfig.sourceUniverseUUID],
    () => api.fetchUniverse(xClusterConfig.sourceUniverseUUID)
  );

  const sourceUniverseTablesQuery = useQuery<YBTable[]>(
    ['universe', xClusterConfig.sourceUniverseUUID, 'tables'],
    () => fetchTablesInUniverse(xClusterConfig.sourceUniverseUUID).then((response) => response.data)
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
          response.data.taskUUID,
          (err: boolean) => {
            if (err) {
              toast.error(
                <span className={styles.alertMsg}>
                  <i className="fa fa-exclamation-circle" />
                  <span>Replication restart failed.</span>
                  <a
                    href={`/tasks/${response.data.taskUUID}`}
                    rel="noopener noreferrer"
                    target="_blank"
                  >
                    View Details
                  </a>
                </span>
              );
            }
            queryClient.invalidateQueries(['Xcluster', xClusterConfig.uuid]);
          },
          // Invalidate the cached data for current xCluster config. The xCluster config status should change to
          // 'in progress' once the task starts.
          () => {
            queryClient.invalidateQueries(['Xcluster', xClusterConfig.uuid]);
          }
        );
      },
      onError: (error: any) => {
        toast.error(
          <span className={styles.alertMsg}>
            <i className="fa fa-exclamation-circle" />
            <span>{error.message}</span>
          </span>
        );
      }
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

  if (sourceUniverseQuery.isError || sourceUniverseTablesQuery.isError) {
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

  const ysqlKeyspaceToTableUUIDs = new Map<string, Set<string>>();
  const ysqlTableUUIDToKeyspace = new Map<string, string>();
  const sourceUniverseTables = sourceUniverseTablesQuery.data;
  sourceUniverseTables.forEach((table) => {
    if (
      table.tableType !== TableType.PGSQL_TABLE_TYPE ||
      table.relationType === YBTableRelationType.INDEX_TABLE_RELATION
    ) {
      // Ignore all index tables and non-YSQL tables.
      return;
    }
    const tableUUIDs = ysqlKeyspaceToTableUUIDs.get(table.keySpace);
    if (tableUUIDs !== undefined) {
      tableUUIDs.add(adaptTableUUID(table.tableUUID));
    } else {
      ysqlKeyspaceToTableUUIDs.set(
        table.keySpace,
        new Set<string>([adaptTableUUID(table.tableUUID)])
      );
    }
    ysqlTableUUIDToKeyspace.set(adaptTableUUID(table.tableUUID), table.keySpace);
  });

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
          ysqlKeyspaceToTableUUIDs,
          ysqlTableUUIDToKeyspace,
          isTableSelectionValidated,
          configTableType,
          setBootstrapRequiredTableUUIDs,
          setFormWarnings
        )
      }
      onFormSubmit={handleFormSubmit}
      initialValues={INITIAL_VALUES}
      submitLabel={submitLabel}
      onHide={() => {
        closeModal();
      }}
      footerAccessory={<YBButton btnClass="btn" btnText={'Cancel'} onClick={closeModal} />}
      render={(formikProps: FormikProps<AddTableFormValues>) => {
        // workaround for outdated version of Formik to access form methods outside of <Formik>
        formik.current = formikProps;
        switch (currentStep) {
          case FormStep.SELECT_TABLES:
            // Casting because FormikValues and FormikError have different types.
            const errors = formik.current.errors as FormikErrors<AddTableFormErrors>;
            return (
              <>
                <div className={styles.formInstruction}>
                  {`1. Select the ${TableTypeLabel[configTableType]} tables you want to add to the xCluster configuration.`}
                </div>
                <TableSelect
                  {...{
                    configAction: XClusterConfigAction.ADD_TABLE,
                    xClusterConfigUUID: xClusterConfig.uuid,
                    sourceUniverseUUID: xClusterConfig.sourceUniverseUUID,
                    targetUniverseUUID: xClusterConfig.targetUniverseUUID,
                    currentStep,
                    setCurrentStep,
                    selectedTableUUIDs: formik.current.values.tableUUIDs,
                    setSelectedTableUUIDs: (tableUUIDs: string[]) =>
                      setSelectedTableUUIDs(tableUUIDs, formik.current),
                    tableType: configTableType,
                    isFixedTableType: true,
                    setTableType: (_) => null,
                    selectedKeyspaces,
                    setSelectedKeyspaces,
                    selectionError: errors.tableUUIDs,
                    selectionWarning: formWarnings?.tableUUIDs
                  }}
                />
              </>
            );
          case FormStep.CONFIGURE_BOOTSTRAP:
            return (
              <>
                <div className={styles.formInstruction}>2. Configure bootstrap</div>
                <ConfigureBootstrapStep formik={formik} />
              </>
            );
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
  ysqlKeyspaceToTableUUIDs: Map<string, Set<string>>,
  ysqlTableUUIDToKeyspace: Map<string, string>,
  isTableSelectionValidated: boolean,
  configTableType: XClusterTableType,
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
      const bootstrapTableUUIDs = await getBootstrapTableUUIDs(
        configTableType,
        values.tableUUIDs,
        sourceUniverse.universeUUID,
        ysqlKeyspaceToTableUUIDs,
        ysqlTableUUIDToKeyspace
      );

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
      if (shouldValidateParallelThread && values.parallelThreads > PARALLEL_THREADS_RANGE.MAX) {
        errors.parallelThreads = `Parallel threads must be less than or equal to ${PARALLEL_THREADS_RANGE.MAX}`;
      } else if (
        shouldValidateParallelThread &&
        values.parallelThreads < PARALLEL_THREADS_RANGE.MIN
      ) {
        errors.parallelThreads = `Parallel threads must be greater than or equal to ${PARALLEL_THREADS_RANGE.MIN}`;
      }

      throw errors;
    }
    default:
      return {};
  }
};

/**
 * Return the UUIDs for tables which require bootstrapping.
 */
const getBootstrapTableUUIDs = async (
  configTableType: XClusterTableType,
  selectedTableUUIDs: string[],
  sourceUniverseUUID: string,
  ysqlKeyspaceToTableUUIDs: Map<string, Set<string>>,
  ysqlTableUUIDToKeyspace: Map<string, string>
) => {
  // Check if bootstrap is required, for each selected table
  const bootstrapTests = await isBootstrapRequired(
    sourceUniverseUUID,
    selectedTableUUIDs.map(adaptTableUUID)
  );
  const bootstrapTableUUIDs = new Set<string>();

  bootstrapTests.forEach((bootstrapTest) => {
    // Each bootstrapTest response is of the form {<tableUUID>: boolean}.
    // Until the backend supports multiple tableUUIDs per request, the response object
    // will only contain one tableUUID.
    // Note: Once backend does support multiple tableUUIDs per request, we will replace this
    //       logic with one that simply filters on the keys (tableUUIDs) of the returned object.
    const tableUUID = Object.keys(bootstrapTest)[0];

    if (bootstrapTest[tableUUID]) {
      switch (configTableType) {
        case TableType.YQL_TABLE_TYPE:
          bootstrapTableUUIDs.add(tableUUID);
          return;
        case TableType.PGSQL_TABLE_TYPE: {
          bootstrapTableUUIDs.add(tableUUID);
          // YSQL ONLY: In addition to the current table, add all other tables in the same keyspace
          //            for bootstrapping.
          const keyspace = ysqlTableUUIDToKeyspace.get(tableUUID);
          if (keyspace !== undefined) {
            const tableUUIDs = ysqlKeyspaceToTableUUIDs.get(keyspace);
            if (tableUUIDs !== undefined) {
              tableUUIDs.forEach((tableUUID) => bootstrapTableUUIDs.add(tableUUID));
            }
          }
          return;
        }
        default:
          assertUnreachableCase(configTableType);
      }
    }
  });
  return Array.from(bootstrapTableUUIDs);
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

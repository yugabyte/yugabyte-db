import React, { useRef, useState } from 'react';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import _ from 'lodash';

import { FormikActions, FormikProps } from 'formik';

import {
  createXClusterReplication,
  fetchTablesInUniverse,
  fetchTaskUntilItCompletes,
  fetchUniverseDiskUsageMetric,
  isBootstrapRequired
} from '../../../actions/xClusterReplication';
import { PARALLEL_THREADS_RANGE } from '../../backupv2/common/BackupUtils';
import { YBModalForm } from '../../common/forms';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { adaptTableUUID } from '../ReplicationUtils';
import { ConfigureBootstrapStep } from './ConfigureBootstrapStep';
import { SelectTablesStep } from './SelectTablesStep';
import { SelectTargetUniverseStep } from './SelectTargetUniverseStep';
import { YBButton, YBModal } from '../../common/forms/fields';
import { api } from '../../../redesign/helpers/api';
import { getPrimaryCluster, isYbcEnabledUniverse } from '../../../utils/UniverseUtils';

import { TableType, Universe } from '../../../redesign/helpers/dtos';
import { XClusterTableType, YBTable } from '../XClusterTypes';

import styles from './CreateConfigModal.module.scss';

export interface CreateXClusterConfigFormValues {
  configName: string;
  targetUniverse: { label: string; value: Universe };
  tableUUIDs: string[];
  // Bootstrap fields
  storageConfig: { label: string; name: string; regions: any[]; value: string };
  parallelThreads: number;
}

export interface CreateXClusterConfigFormErrors {
  configName: string;
  targetUniverse: string;
  tableUUIDs: { title: string; body: string };
  // Bootstrap fields
  storageConfig: string;
  parallelThreads: string;
}

interface ConfigureReplicationModalProps {
  onHide: Function;
  visible: boolean;
  currentUniverseUUID: string;
}

const MODAL_TITLE = 'Configure Replication';

export const FormStep = {
  SELECT_TARGET_UNIVERSE: {
    submitLabel: 'Next: Select Tables'
  } as const,
  SELECT_TABLES: {
    submitLabel: 'Validate Connectivity and Schema'
  } as const,
  ENABLE_REPLICATION: {
    submitLabel: 'Enable Replication'
  } as const,
  CONFIGURE_BOOTSTRAP: {
    submitLabel: 'Bootstrap and Enable Replication'
  } as const
} as const;

export type FormStep = typeof FormStep[keyof typeof FormStep];

const FIRST_FORM_STEP = FormStep.SELECT_TARGET_UNIVERSE;
const DEFAULT_TABLE_TYPE = TableType.PGSQL_TABLE_TYPE;

const INITIAL_VALUES: Partial<CreateXClusterConfigFormValues> = {
  configName: '',
  tableUUIDs: [],
  // Bootstrap fields
  parallelThreads: PARALLEL_THREADS_RANGE.MIN
};

// Validation Constants
const MIN_FREE_DISK_SPACE_GB = 100;

export const CreateConfigModal = ({
  onHide,
  visible,
  currentUniverseUUID
}: ConfigureReplicationModalProps) => {
  const [currentStep, setCurrentStep] = useState<FormStep>(FIRST_FORM_STEP);
  const [bootstrapRequiredTableUUIDs, setBootstrapRequiredTableUUIDs] = useState<string[]>([]);

  // SelectTablesStep.tsx state
  // Need to store this in CreateConfigModal.tsx to support navigating back to this step.
  const [tableType, setTableType] = useState<XClusterTableType>(DEFAULT_TABLE_TYPE);
  const [selectedKeyspaces, setSelectedKeyspaces] = useState<string[]>([]);

  const queryClient = useQueryClient();
  const formik = useRef({} as FormikProps<CreateXClusterConfigFormValues>);

  const xClusterConfigMutation = useMutation(
    (values: CreateXClusterConfigFormValues) => {
      if (bootstrapRequiredTableUUIDs.length > 0) {
        const bootstrapParams = {
          tables: bootstrapRequiredTableUUIDs,
          backupRequestParams: {
            storageConfigUUID: values.storageConfig.value,
            parallelism: values.parallelThreads,
            sse: values.storageConfig.name === 'S3',
            universeUUID: null
          }
        };
        return createXClusterReplication(
          values.targetUniverse.value.universeUUID,
          currentUniverseUUID,
          values.configName,
          values.tableUUIDs.map(adaptTableUUID),
          bootstrapParams
        );
      }
      return createXClusterReplication(
        values.targetUniverse.value.universeUUID,
        currentUniverseUUID,
        values.configName,
        values.tableUUIDs.map(adaptTableUUID)
      );
    },
    {
      onSuccess: (resp) => {
        closeModal();
        // This newly xCluster config will be added to sourceXClusterConfigs for the current universe.
        // Invalidate current universe query to fetch fresh sourceXClusterConfigs.
        queryClient.invalidateQueries(['universe', currentUniverseUUID], { exact: true });

        fetchTaskUntilItCompletes(resp.data.taskUUID, (err: boolean) => {
          if (err) {
            toast.error(
              <span className={styles.alertMsg}>
                <i className="fa fa-exclamation-circle" />
                <span>Replication creation failed.</span>
                <a href={`/tasks/${resp.data.taskUUID}`} rel="noopener noreferrer" target="_blank">
                  View Details
                </a>
              </span>
            );
          }
          queryClient.invalidateQueries(['universe', currentUniverseUUID], { exact: true });
        });
      },
      onError: (err: any) => {
        toast.error(err.response.data.error);
      }
    }
  );

  const resetModalState = () => {
    setCurrentStep(FormStep.SELECT_TARGET_UNIVERSE);
    setBootstrapRequiredTableUUIDs([]);
    setTableType(DEFAULT_TABLE_TYPE);
    setSelectedKeyspaces([]);
  };

  const closeModal = () => {
    resetModalState();
    onHide();
  };

  const tablesQuery = useQuery<YBTable[]>(['universe', currentUniverseUUID, 'tables'], () =>
    fetchTablesInUniverse(currentUniverseUUID).then((res) => res.data)
  );

  const universeQuery = useQuery<Universe>(['universe', currentUniverseUUID], () =>
    api.fetchUniverse(currentUniverseUUID)
  );

  const handleFormSubmit = async (
    values: CreateXClusterConfigFormValues,
    actions: FormikActions<CreateXClusterConfigFormValues>
  ) => {
    if (
      currentStep === FormStep.ENABLE_REPLICATION ||
      currentStep === FormStep.CONFIGURE_BOOTSTRAP
    ) {
      if (values.tableUUIDs.length === 0) {
        toast.error('Configuration must have at least one table');
        actions.setSubmitting(false);
        return;
      }
      xClusterConfigMutation.mutate(values);
    } else if (currentStep === FormStep.SELECT_TARGET_UNIVERSE) {
      setCurrentStep(FormStep.SELECT_TABLES);
    } else if (currentStep === FormStep.SELECT_TABLES) {
      if (bootstrapRequiredTableUUIDs.length > 0) {
        setCurrentStep(FormStep.CONFIGURE_BOOTSTRAP);
      } else {
        setCurrentStep(FormStep.ENABLE_REPLICATION);
      }
    }
    actions.setSubmitting(false);
  };

  const handleBackNavigation = () => {
    switch (currentStep) {
      case FormStep.SELECT_TABLES:
        setCurrentStep(FormStep.SELECT_TARGET_UNIVERSE);
        break;
      case FormStep.ENABLE_REPLICATION:
      case FormStep.CONFIGURE_BOOTSTRAP:
        setCurrentStep(FormStep.SELECT_TABLES);
    }
  };

  if (tablesQuery.isLoading || universeQuery.isLoading) {
    return (
      <YBModal
        size="large"
        title={MODAL_TITLE}
        visible={visible}
        onHide={() => {
          closeModal();
        }}
        submitLabel={currentStep.submitLabel}
      >
        <YBLoading />
      </YBModal>
    );
  }

  if (
    tablesQuery.isError ||
    universeQuery.isError ||
    tablesQuery.data === undefined ||
    universeQuery.data === undefined
  ) {
    return (
      <YBModal
        size="large"
        title={MODAL_TITLE}
        visible={visible}
        onHide={() => {
          closeModal();
        }}
      >
        <YBErrorIndicator />
      </YBModal>
    );
  }

  return (
    <YBModalForm
      size="large"
      title={MODAL_TITLE}
      visible={visible}
      validate={(values: CreateXClusterConfigFormValues) =>
        validateForm(values, currentStep, universeQuery.data, setBootstrapRequiredTableUUIDs)
      }
      // Perform validation for select table when user submits.
      validateOnChange={currentStep !== FormStep.SELECT_TABLES}
      validateOnBlur={currentStep !== FormStep.SELECT_TABLES}
      onFormSubmit={handleFormSubmit}
      initialValues={INITIAL_VALUES}
      submitLabel={currentStep.submitLabel}
      onHide={() => {
        closeModal();
      }}
      footerAccessory={
        currentStep === FIRST_FORM_STEP ? (
          <YBButton btnClass="btn" btnText={'Cancel'} onClick={closeModal} />
        ) : (
          <YBButton btnClass="btn" btnText={'Back'} onClick={handleBackNavigation} />
        )
      }
      render={(formikProps: FormikProps<CreateXClusterConfigFormValues>) => {
        // workaround for outdated version of Formik to access form methods outside of <Formik>
        formik.current = formikProps;

        if (tablesQuery.isLoading || universeQuery.isLoading) {
          return <YBLoading />;
        }

        if (
          tablesQuery.isError ||
          universeQuery.isError ||
          tablesQuery.data === undefined ||
          universeQuery.data === undefined
        ) {
          return <YBErrorIndicator />;
        }

        switch (currentStep) {
          case FormStep.SELECT_TARGET_UNIVERSE:
            return (
              <SelectTargetUniverseStep
                {...{
                  formik,
                  currentUniverseUUID
                }}
              />
            );
          case FormStep.SELECT_TABLES:
          case FormStep.ENABLE_REPLICATION:
            return (
              <SelectTablesStep
                {...{
                  formik,
                  sourceTables: tablesQuery.data,
                  currentUniverseUUID,
                  currentStep,
                  setCurrentStep,
                  tableType,
                  setTableType,
                  selectedKeyspaces,
                  setSelectedKeyspaces
                }}
              />
            );
          case FormStep.CONFIGURE_BOOTSTRAP:
            return (
              <ConfigureBootstrapStep
                {...{
                  formik,
                  bootstrapRequiredTableUUIDs,
                  sourceTables: tablesQuery.data
                }}
              />
            );
        }

        // Needed to resolve "not all code paths return a value" error.
        // This return should not be reach as we are covering all possible values
        // of FormStep in the switch.
        return <YBErrorIndicator />;
      }}
    />
  );
};

const validateForm = async (
  values: CreateXClusterConfigFormValues,
  formStep: FormStep,
  currentUniverse: Universe,
  setBootstrapRequiredTableUUIDs: (tableUUIDs: string[]) => void
) => {
  // Since our formik verision is < 2.0 , we need to throw errors instead of
  // returning them in custom async validation:
  // https://github.com/jaredpalmer/formik/issues/1392#issuecomment-606301031

  switch (formStep) {
    case FormStep.SELECT_TARGET_UNIVERSE: {
      const errors: Partial<CreateXClusterConfigFormErrors> = {};

      if (!values.configName) {
        errors.configName = 'Replication name is required.';
      }
      if (!values.targetUniverse) {
        errors.targetUniverse = 'Target universe is required.';
      } else if (
        getPrimaryCluster(values.targetUniverse.value.universeDetails.clusters)?.userIntent
          ?.enableNodeToNodeEncrypt !==
        getPrimaryCluster(currentUniverse?.universeDetails.clusters)?.userIntent
          ?.enableNodeToNodeEncrypt
      ) {
        errors.targetUniverse =
          'The target universe must have the same Encryption in-Transit (TLS) configuration as the source universe. Edit the TLS configuration to proceed.';
      } else if (
        !_.isEqual(
          values.targetUniverse?.value?.universeDetails?.encryptionAtRestConfig,
          currentUniverse?.universeDetails?.encryptionAtRestConfig
        )
      ) {
        errors.targetUniverse =
          'The target universe must have the same key management system (KMS) configuration as the source universe. Edit the KMS configuration to proceed.';
      }

      throw errors;
    }
    case FormStep.SELECT_TABLES: {
      const errors: Partial<CreateXClusterConfigFormErrors> = {};

      if (!values.tableUUIDs || values.tableUUIDs.length === 0) {
        errors.tableUUIDs = {
          title: 'No tables selected.',
          body: 'Select at least 1 table to proceed'
        };
      }

      // Check if bootstrap is required, for each selected table
      const bootstrapTests = await isBootstrapRequired(
        currentUniverse.universeUUID,
        values.tableUUIDs.map(adaptTableUUID)
      );

      const bootstrapTableUUIDs = bootstrapTests.reduce(
        (bootstrapTableUUIDs: string[], bootstrapTest) => {
          // Each bootstrapTest response is of the form {<tableUUID>: boolean}.
          // Until the backend supports multiple tableUUIDs per request, the response object
          // will only contain one tableUUID.
          // Note: Once backend does support multiple tableUUIDs per request, we will replace this
          //       logic with one that simply filters on the keys (tableUUIDs) of the returned object.
          const tableUUID = Object.keys(bootstrapTest)[0];

          if (bootstrapTest[tableUUID]) {
            bootstrapTableUUIDs.push(tableUUID);
          }
          return bootstrapTableUUIDs;
        },
        []
      );
      setBootstrapRequiredTableUUIDs(bootstrapTableUUIDs);

      // If some tables require bootstrapping, we need to validate the source universe has enough
      // disk space.
      if (bootstrapTableUUIDs.length > 0) {
        // Disk space validation
        const currentUniverseNodePrefix = currentUniverse.universeDetails.nodePrefix;
        const diskUsageMetric = await fetchUniverseDiskUsageMetric(currentUniverseNodePrefix);
        const freeSpaceTrace = diskUsageMetric.disk_usage.data.find(
          (trace) => trace.name === 'free'
        );
        const freeDiskSpace = freeSpaceTrace?.y[freeSpaceTrace.y.length - 1];

        if (freeDiskSpace !== undefined && freeDiskSpace < MIN_FREE_DISK_SPACE_GB) {
          errors.tableUUIDs = {
            title: 'Insufficient disk space.',
            body: `Some selected tables require bootstrapping. Please ensure the source universe has at least ${MIN_FREE_DISK_SPACE_GB} GB of free disk space.`
          };
        }
      }

      throw errors;
    }
    case FormStep.CONFIGURE_BOOTSTRAP: {
      const errors: Partial<CreateXClusterConfigFormErrors> = {};
      if (!values.storageConfig) {
        errors.storageConfig = 'Backup storage configuration is required.';
      }
      const shouldValidateParallelThread =
        values.parallelThreads && isYbcEnabledUniverse(currentUniverse?.universeDetails);
      if (shouldValidateParallelThread && values.parallelThreads > PARALLEL_THREADS_RANGE.MAX) {
        errors.parallelThreads = `Parallel threads must be less than or equal to ${PARALLEL_THREADS_RANGE.MAX}`;
      } else if (
        shouldValidateParallelThread &&
        values.parallelThreads > PARALLEL_THREADS_RANGE.MIN
      ) {
        errors.parallelThreads = `Parallel threads must be greater than or equal to ${PARALLEL_THREADS_RANGE.MIN}`;
      }

      throw errors;
    }
  }
  return {};
};

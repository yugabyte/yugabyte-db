import React, { useRef, useState } from 'react';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import * as Yup from 'yup';
import { FormikActions, FormikProps } from 'formik';

import {
  createXClusterReplication,
  fetchTablesInUniverse,
  fetchTaskUntilItCompletes,
  isBootstrapRequired
} from '../../../actions/xClusterReplication';
import { PARALLEL_THREADS_RANGE } from '../../backupv2/common/BackupUtils';
import { YBModalForm } from '../../common/forms';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { adaptTableUUID } from '../ReplicationUtils';
import { ConfigureBootstrapStep } from './ConfigureBootstrapStep';
import { SelectTablesStep } from './SelectTablesStep';
import { SelectTargetUniverseStep } from './SelectTargetUniverseStep';
import { YBButton } from '../../common/forms/fields';
import { api } from '../../../redesign/helpers/api';
import { isYbcEnabledUniverse } from '../../../utils/UniverseUtils';

import { TableType, Universe } from '../../../redesign/helpers/dtos';
import { XClusterTableType, YBTable } from '../XClusterTypes';

import styles from './CreateConfigModal.module.scss';

export interface CreateXClusterConfigFormValues {
  configName: string;
  targetUniverseUUID: { label: string; value: string };
  tableUUIDs: string[];
  // Bootstrap fields
  storageConfig: { label: string; name: string; regions: any[]; value: string };
  parallelThreads: number;
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
  targetUniverseUUID: { label: '', value: '' },
  tableUUIDs: [],
  // Bootstrap fields
  parallelThreads: PARALLEL_THREADS_RANGE.MIN
};

export const CreateConfigModal = ({
  onHide,
  visible,
  currentUniverseUUID
}: ConfigureReplicationModalProps) => {
  const [currentStep, setCurrentStep] = useState<FormStep>(FIRST_FORM_STEP);
  const [bootstrapRequiredTableUUIDs, setBootstrapRequiredTables] = useState<string[]>([]);

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
          values.targetUniverseUUID.value,
          currentUniverseUUID,
          values.configName,
          values.tableUUIDs.map(adaptTableUUID),
          bootstrapParams
        );
      }
      return createXClusterReplication(
        values.targetUniverseUUID.value,
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
    setBootstrapRequiredTables([]);
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
      // Check if bootstrap is required, for each selected table
      const bootstrapTests = await isBootstrapRequired(
        currentUniverseUUID,
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
      setBootstrapRequiredTables(bootstrapTableUUIDs);

      if (bootstrapTableUUIDs.length > 0) {
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

  return (
    <YBModalForm
      size="large"
      title={MODAL_TITLE}
      visible={visible}
      validationSchema={getValidationSchema(currentStep, universeQuery.data)}
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


// This will be replaced with new validation logic in 
// https://yugabyte.atlassian.net/browse/PLAT-5078
const getValidationSchema = (formStep: FormStep, universe: Universe | undefined) => {
  switch (formStep) {
    case FormStep.SELECT_TARGET_UNIVERSE:
      return Yup.object().shape({
        configName: Yup.string().required('Replication name is required'),
        targetUniverseUUID: Yup.string().required('Target universe UUID is required')
      });
    case FormStep.CONFIGURE_BOOTSTRAP:
      return Yup.object().shape({
        storageConfig: Yup.object().nullable().required('Required'),
        ...(universe?.universeDetails &&
          isYbcEnabledUniverse(universe.universeDetails) && {
            parallelThreads: Yup.number()
              .min(
                PARALLEL_THREADS_RANGE.MIN,
                `Parallel threads must be greater than or equal to ${PARALLEL_THREADS_RANGE.MIN}`
              )
              .max(
                PARALLEL_THREADS_RANGE.MAX,
                `Parallel threads must be less than or equal to ${PARALLEL_THREADS_RANGE.MAX}`
              )
          })
      });
  }

  // No validation required for the remaining steps.
  return undefined;
};

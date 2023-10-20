import { makeStyles, Typography } from '@material-ui/core';
import { AxiosError } from 'axios';
import { useState } from 'react';
import { SubmitHandler, useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';

import {
  editXClusterConfigTables,
  fetchTablesInUniverse,
  fetchTaskUntilItCompletes,
  fetchUniverseDiskUsageMetric
} from '../../../../actions/xClusterReplication';
import { YBModal, YBModalProps } from '../../../../redesign/components';
import { api, universeQueryKey, xClusterQueryKey } from '../../../../redesign/helpers/api';
import { handleServerError } from '../../../../utils/errorHandlingUtils';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import { TableSelect } from '../../sharedComponents/tableSelect/TableSelect';
import { BOOTSTRAP_MIN_FREE_DISK_SPACE_GB, XClusterConfigAction } from '../../constants';
import {
  formatUuidForXCluster,
  formatUuidFromXCluster,
  getTablesForBootstrapping,
  getXClusterConfigTableType,
  parseFloatIfDefined
} from '../../ReplicationUtils';

import { Universe, UniverseNamespace, YBTable } from '../../../../redesign/helpers/dtos';
import { XClusterConfig } from '../../XClusterTypes';

interface EditTablesModalProps {
  xClusterConfig: XClusterConfig;
  isDrConfig: boolean;
  modalProps: YBModalProps;
}

interface EditTablesFormValues {
  tableUuids: string[];
  namespaceUuids: string[];
}

const useStyles = makeStyles((theme) => ({
  toastContainer: {
    display: 'flex',
    gap: theme.spacing(0.5),
    '& a': {
      textDecoration: 'underline',
      color: '#fff'
    }
  }
}));

const MODAL_NAME = 'EditTablesModal';
const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.config.editTablesModal';
export const EditTablesModal = ({
  isDrConfig,
  modalProps,
  xClusterConfig
}: EditTablesModalProps) => {
  const [selectionError, setSelectionError] = useState<{ title: string; body: string }>();
  const [selectionWarning, setSelectionWarning] = useState<{ title: string; body: string }>();
  const [isTableSelectionValidated, setIsTableSelectionValidated] = useState<boolean>(false);

  const queryClient = useQueryClient();
  const classes = useStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const sourceUniverseQuery = useQuery<Universe>(
    universeQueryKey.detail(xClusterConfig.sourceUniverseUUID),
    () => api.fetchUniverse(xClusterConfig.sourceUniverseUUID)
  );

  const sourceUniverseTablesQuery = useQuery<YBTable[]>(
    universeQueryKey.tables(xClusterConfig.sourceUniverseUUID, {
      excludeColocatedTables: true
    }),
    () =>
      fetchTablesInUniverse(xClusterConfig.sourceUniverseUUID, {
        excludeColocatedTables: true
      }).then((response) => response.data)
  );

  const sourceUniverseNamespacesQuery = useQuery<UniverseNamespace[]>(
    universeQueryKey.namespaces(xClusterConfig.sourceUniverseUUID),
    () => api.fetchUniverseNamespaces(xClusterConfig.sourceUniverseUUID)
  );

  const editTableMutation = useMutation(
    (formValues: EditTablesFormValues) => {
      return editXClusterConfigTables(xClusterConfig.uuid, formValues.tableUuids);
    },
    {
      onSuccess: (response) => {
        const invalidateQueries = () =>
          queryClient.invalidateQueries(xClusterQueryKey.detail(xClusterConfig.uuid));
        const handleTaskCompletion = (error: boolean) => {
          if (error) {
            toast.error(
              <span className={classes.toastContainer}>
                <i className="fa fa-exclamation-circle" />
                <span>{t('error.taskFailure')}</span>
                <a href={`/tasks/${response.taskUUID}`} rel="noopener noreferrer" target="_blank">
                  {t('viewDetails', { keyPrefix: 'task' })}
                </a>
              </span>
            );
          } else {
            toast.success(t('success.taskSuccess'));
          }
        };
        modalProps.onClose();
        fetchTaskUntilItCompletes(response.taskUUID, handleTaskCompletion, invalidateQueries);
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, { customErrorLabel: t('error.requestFailure') })
    }
  );

  const formMethods = useForm<EditTablesFormValues>({
    defaultValues:
      sourceUniverseTablesQuery.data && sourceUniverseNamespacesQuery.data
        ? getDefaultFormValues(
            xClusterConfig,
            sourceUniverseTablesQuery.data,
            sourceUniverseNamespacesQuery.data
          )
        : {}
  });

  const modalTitle = t('title');
  const submitLabel = isTableSelectionValidated
    ? t('submitButton.applyChanges')
    : t('submitButton.validate');
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
        submitLabel={submitLabel}
        cancelLabel={cancelLabel}
        buttonProps={{
          primary: { disabled: true }
        }}
        submitTestId={`${MODAL_NAME}-SubmitButton`}
        cancelTestId={`${MODAL_NAME}-CancelButton`}
        {...modalProps}
      >
        <YBLoading />
      </YBModal>
    );
  }

  const xClusterConfigTableType = getXClusterConfigTableType(xClusterConfig);
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
      ? t('error.undefinedXClusterTableType')
      : t('error.fetchSourceUniverseDetailsFailure');
    return (
      <YBModal
        title={modalTitle}
        submitLabel={submitLabel}
        cancelLabel={cancelLabel}
        buttonProps={{
          primary: { disabled: true }
        }}
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
    // We need to update the defaultValues with reset() after regionMetadataQuery is successful.
    formMethods.reset(
      getDefaultFormValues(xClusterConfig, sourceUniverseTables, sourceUniverseNamespaces)
    );
  }

  const sourceUniverse = sourceUniverseQuery.data;
  const onSubmit: SubmitHandler<EditTablesFormValues> = async (formValues) => {
    if (formValues.tableUuids.length <= 0) {
      formMethods.setError('tableUuids', {
        type: 'min',
        message: t('error.validationMinimumTableUuids')
      });
      // The TableSelect component expects error objects with title and body fields.
      // React-hook-form only allows string error messages.
      // Thus, we need an store these error objects separately.
      setSelectionError({
        title: 'No tables selected.',
        body: 'Select at least 1 table to proceed'
      });
      return;
    }

    if (!isTableSelectionValidated) {
      let bootstrapTableUuids: string[] | null = null;
      try {
        // We pass null as the target universe in the following method because add table does not
        // support the case where a matching table does not exist on the target universe.
        bootstrapTableUuids = await getTablesForBootstrapping(
          formValues.tableUuids,
          sourceUniverseUuid,
          null /* targetUniverseUUID */,
          sourceUniverseTables,
          xClusterConfig.type
        );
      } catch (error: any) {
        toast.error(
          <span className={classes.toastContainer}>
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
        setSelectionWarning({
          title: 'Table bootstrap verification error',
          body:
            'An error occured while verifying whether the selected tables require bootstrapping.'
        });
      }

      if ((bootstrapTableUuids?.length ?? []) > 0) {
        // Validate that the source universe has at least the recommeneded amount of
        // disk space if bootstrapping is required.
        const currentUniverseNodePrefix = sourceUniverse.universeDetails.nodePrefix;
        const diskUsageMetric = await fetchUniverseDiskUsageMetric(currentUniverseNodePrefix);
        const freeSpaceTrace = diskUsageMetric.disk_usage.data.find(
          (trace) => trace.name === 'free'
        );
        const freeDiskSpace = parseFloatIfDefined(freeSpaceTrace?.y[freeSpaceTrace.y.length - 1]);

        if (freeDiskSpace !== undefined && freeDiskSpace < BOOTSTRAP_MIN_FREE_DISK_SPACE_GB) {
          setSelectionWarning({
            title: 'Insufficient disk space.',
            body: `Some selected tables require bootstrapping. We recommend having at least ${BOOTSTRAP_MIN_FREE_DISK_SPACE_GB} GB of free disk space in the source universe.`
          });
        } else {
          setSelectionWarning({
            title: 'Bootstrapping required.',
            body: `Some selected tables require bootstrapping.`
          });
        }
      }

      if (selectionError === undefined) {
        setIsTableSelectionValidated(true);
      }
      return;
    }

    editTableMutation.mutate(formValues);
  };

  const setSelectedTableUuids = (tableUuids: string[]) => {
    setIsTableSelectionValidated(false);
    formMethods.setValue('tableUuids', tableUuids, { shouldValidate: false });
  };
  const setSelectedNamespaces = (namespaces: string[]) => {
    setIsTableSelectionValidated(false);
    formMethods.setValue('namespaceUuids', namespaces, { shouldValidate: false });
  };

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
      overrideWidth="960px"
      {...modalProps}
    >
      <Typography variant="body1">{t('instruction')}</Typography>
      <TableSelect
        {...{
          configAction: XClusterConfigAction.MANAGE_TABLE,
          isDrConfig,
          isFixedTableType: true, // Users are not allowed to change xCluster table type after creation.
          selectedKeyspaces: selectedNamespaceUuids,
          selectedTableUUIDs: selectedTableUuids,
          selectionError,
          selectionWarning,
          setSelectedKeyspaces: setSelectedNamespaces,
          setSelectedTableUUIDs: setSelectedTableUuids,
          setTableType: (_) => null, // Users are not allowed to change xCluster table type after creation.
          sourceUniverseUUID: sourceUniverseUuid,
          tableType: xClusterConfigTableType,
          targetUniverseUUID: targetUniverseUuid,
          xClusterConfigUUID: xClusterConfig.uuid
        }}
      />
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
    if (selectedTableUuids.has(formatUuidForXCluster(table.tableUUID))) {
      selectedNamespaceUuid.add(namespaceToNamespaceUuid[table.keySpace]);
    }
  });
  return Array.from(selectedNamespaceUuid);
};

const getDefaultFormValues = (
  xClusterConfig: XClusterConfig,
  sourceUniverseTables: YBTable[],
  sourceUniverseNamespace: UniverseNamespace[]
): Partial<EditTablesFormValues> => {
  return {
    tableUuids: xClusterConfig.tables.map(formatUuidFromXCluster),
    namespaceUuids: getXClusterConfigNamespaces(
      xClusterConfig,
      sourceUniverseTables,
      sourceUniverseNamespace
    )
  };
};

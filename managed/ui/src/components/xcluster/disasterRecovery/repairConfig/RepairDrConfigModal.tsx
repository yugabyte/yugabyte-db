import clsx from 'clsx';
import { Box, FormHelperText, makeStyles, Typography, useTheme } from '@material-ui/core';
import { Controller, SubmitHandler, useForm } from 'react-hook-form';
import { Trans, useTranslation } from 'react-i18next';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import { AxiosError } from 'axios';

import { YBModal, YBModalProps, YBTooltip } from '../../../../redesign/components';
import { YBReactSelectField } from '../../../configRedesign/providerRedesign/components/YBReactSelect/YBReactSelectField';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import {
  api,
  drConfigQueryKey,
  ReplaceDrReplicaRequest,
  universeQueryKey,
  xClusterQueryKey
} from '../../../../redesign/helpers/api';
import {
  fetchTaskUntilItCompletes,
  restartXClusterConfig
} from '../../../../actions/xClusterReplication';
import { UnavailableUniverseStates } from '../../../../redesign/helpers/constants';
import { getUniverseStatus } from '../../../universes/helpers/universeHelpers';
import { assertUnreachableCase, handleServerError } from '../../../../utils/errorHandlingUtils';
import {
  ReactSelectStorageConfigField,
  StorageConfigOption
} from '../../sharedComponents/ReactSelectStorageConfig';
import { ReactComponent as SelectedIcon } from '../../../../redesign/assets/circle-selected.svg';
import { ReactComponent as UnselectedIcon } from '../../../../redesign/assets/circle-empty.svg';
import { ReactComponent as InfoIcon } from '../../../../redesign/assets/info-message.svg';
import { getXClusterConfig } from '../utils';

import { Universe } from '../../../../redesign/helpers/dtos';
import { DrConfig } from '../dtos';

import toastStyles from '../../../../redesign/styles/toastStyles.module.scss';
import { ApiPermissionMap } from '../../../../redesign/features/rbac/ApiAndUserPermMapping';
import { RbacValidator } from '../../../../redesign/features/rbac/common/RbacApiPermValidator';

interface RepairDrConfigModalProps {
  drConfig: DrConfig;
  modalProps: YBModalProps;
}

interface RepairDrConfigModalFormValues {
  storageConfig: StorageConfigOption;

  repairType?: RepairType;
  targetUniverse?: { value: Universe; label: string };
}

const useStyles = makeStyles((theme) => ({
  instructionsHeader: {
    marginBottom: theme.spacing(3)
  },
  optionCard: {
    display: 'flex',
    flexDirection: 'column',

    minHeight: '188px',
    padding: `${theme.spacing(2)}px ${theme.spacing(3)}px`,

    background: theme.palette.ybacolors.backgroundGrayLightest,
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: '8px',

    '&:hover': {
      cursor: 'pointer'
    },
    '&$selected': {
      background: theme.palette.ybacolors.backgroundBlueLight,
      border: `1px solid ${theme.palette.ybacolors.borderBlue}`
    },
    '&$disabled': {
      '&:hover': {
        cursor: 'not-allowed'
      }
    }
  },
  selected: {},
  disabled: {},
  optionCardHeader: {
    display: 'flex',
    alignItems: 'center',

    marginBottom: theme.spacing(3)
  },
  fieldLabel: {
    display: 'flex',
    gap: theme.spacing(1),
    alignItems: 'center',

    marginBottom: theme.spacing(1)
  },
  infoIcon: {
    '&:hover': {
      cursor: 'pointer'
    }
  },
  dialogContentRoot: {
    display: 'flex',
    flexDirection: 'column'
  },
  toastContainer: {
    display: 'flex',
    gap: theme.spacing(0.5),
    '& a': {
      textDecoration: 'underline',
      color: '#fff'
    }
  }
}));

const RepairType = {
  USE_EXISITING_TARGET_UNIVERSE: 'useExistingTargetUniverse',
  USE_NEW_TARGET_UNIVERSE: 'useNewTargetUniverse'
} as const;
type RepairType = typeof RepairType[keyof typeof RepairType];

const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.repairDrConfigModal';

export const RepairDrConfigModal = ({ drConfig, modalProps }: RepairDrConfigModalProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const theme = useTheme();
  const classes = useStyles();
  const queryClient = useQueryClient();
  const formMethods = useForm<RepairDrConfigModalFormValues>();

  const universeListQuery = useQuery<Universe[]>(universeQueryKey.ALL, () =>
    api.fetchUniverseList()
  );
  const replaceDrReplicaMutation = useMutation(
    (replaceDrReplicaRequest: ReplaceDrReplicaRequest) => {
      return api.replaceDrReplica(drConfig.uuid, replaceDrReplicaRequest);
    },
    {
      onSuccess: (response, replaceDrReplicaRequest) => {
        const invalidateQueries = () => {
          queryClient.invalidateQueries(drConfigQueryKey.ALL, { exact: true });
          queryClient.invalidateQueries(drConfigQueryKey.detail(drConfig.uuid));

          // Refetch the participating universes and the new target universe to update
          // universe status and references to the DR config.
          queryClient.invalidateQueries(universeQueryKey.detail(drConfig.primaryUniverseUuid), {
            exact: true
          });
          queryClient.invalidateQueries(universeQueryKey.detail(drConfig.drReplicaUniverseUuid), {
            exact: true
          });
          queryClient.invalidateQueries(
            universeQueryKey.detail(replaceDrReplicaRequest.drReplicaUniverseUuid),
            {
              exact: true
            }
          );
        };
        const handleTaskCompletion = (error: boolean) => {
          if (error) {
            toast.error(
              <span className={toastStyles.toastMessage}>
                <i className="fa fa-exclamation-circle" />
                <Typography variant="body2" component="span">
                  {t('error.changeTargetTaskFailure')}
                </Typography>
                <a href={`/tasks/${response.taskUUID}`} rel="noopener noreferrer" target="_blank">
                  {t('viewDetails', { keyPrefix: 'task' })}
                </a>
              </span>
            );
          } else {
            toast.success(
              <Typography variant="body2" component="span">
                {t('success.changeTargetTaskSuccess')}
              </Typography>
            );
          }
          invalidateQueries();
        };

        toast.success(
          <Typography variant="body2" component="span">
            {t('success.changeTargetRequestSuccess')}
          </Typography>
        );
        modalProps.onClose();
        fetchTaskUntilItCompletes(response.taskUUID, handleTaskCompletion, invalidateQueries);
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, { customErrorLabel: t('error.changeTargetRequestFailureLabel') })
    }
  );

  const xClusterConfig = getXClusterConfig(drConfig);
  const restartConfigMutation = useMutation(
    (storageConfigUuid: string) => {
      return restartXClusterConfig(xClusterConfig.uuid, [], {
        backupRequestParams: { storageConfigUUID: storageConfigUuid }
      });
    },
    {
      onSuccess: (response) => {
        const invalidateQueries = () => {
          queryClient.invalidateQueries(drConfigQueryKey.ALL, { exact: true });
          queryClient.invalidateQueries(drConfigQueryKey.detail(drConfig.uuid));
          queryClient.invalidateQueries(xClusterQueryKey.detail(xClusterConfig.uuid));

          // Refetch the participating universes and the new target universe to update
          // universe status and references to the DR config.
          queryClient.invalidateQueries(universeQueryKey.detail(drConfig.primaryUniverseUuid), {
            exact: true
          });
          queryClient.invalidateQueries(universeQueryKey.detail(drConfig.drReplicaUniverseUuid), {
            exact: true
          });
        };
        const handleTaskCompletion = (error: boolean) => {
          if (error) {
            toast.error(
              <span className={toastStyles.toastMessage}>
                <i className="fa fa-exclamation-circle" />
                <Typography variant="body2" component="span">
                  {t('error.restartTaskFailure')}
                </Typography>
                <a href={`/tasks/${response.taskUUID}`} rel="noopener noreferrer" target="_blank">
                  {t('viewDetails', { keyPrefix: 'task' })}
                </a>
              </span>
            );
          } else {
            toast.success(
              <Typography variant="body2" component="span">
                {t('success.restartTaskSuccess')}
              </Typography>
            );
          }
          invalidateQueries();
        };

        toast.success(
          <Typography variant="body2" component="span">
            {t('success.restartRequestSuccess')}
          </Typography>
        );
        modalProps.onClose();
        fetchTaskUntilItCompletes(response.taskUUID, handleTaskCompletion, invalidateQueries);
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, { customErrorLabel: t('error.restartRequestFailureLabel') })
    }
  );

  if (!drConfig.primaryUniverseUuid || !drConfig.drReplicaUniverseUuid) {
    const i18nKey = drConfig.primaryUniverseUuid
      ? 'undefinedTargetUniverseUuid'
      : 'undefinedSourceUniverseUuid';
    return (
      <YBErrorIndicator
        customErrorMessage={t(i18nKey, {
          keyPrefix: 'clusterDetail.xCluster.error'
        })}
      />
    );
  }
  if (universeListQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('failedToFetchUniverseList', { keyPrefix: 'queryError' })}
      />
    );
  }

  if (universeListQuery.isLoading || universeListQuery.isIdle) {
    return <YBLoading />;
  }

  const universeList = universeListQuery.data;
  const sourceUniverseUuid = drConfig.primaryUniverseUuid;
  const targetUniverseUuid = drConfig.drReplicaUniverseUuid;
  const sourceUniverse = universeList.find(
    (universe: Universe) => universe.universeUUID === sourceUniverseUuid
  );
  const targetUniverse = universeList.find(
    (universe: Universe) => universe.universeUUID === targetUniverseUuid
  );

  if (!sourceUniverse || !targetUniverse) {
    const i18nKey = sourceUniverse ? 'failedToFindTargetUniverse' : 'failedToFindSourceUniverse';
    const universeUuid = sourceUniverse ? targetUniverseUuid : sourceUniverseUuid;
    return (
      <YBErrorIndicator
        customErrorMessage={t(i18nKey, {
          keyPrefix: 'clusterDetail.xCluster.error',
          universeUuid: universeUuid
        })}
      />
    );
  }
  const onSubmit: SubmitHandler<RepairDrConfigModalFormValues> = (formValues) => {
    if (!formValues.repairType) {
      // This shouldn't be reached.
      // Field level validation should have prevented form submission if repairType is not defined.
      return;
    }

    const storageConfigUuid = formValues.storageConfig.value.uuid;
    switch (formValues.repairType) {
      case RepairType.USE_EXISITING_TARGET_UNIVERSE:
        return restartConfigMutation.mutateAsync(storageConfigUuid);
      case RepairType.USE_NEW_TARGET_UNIVERSE:
        if (formValues.targetUniverse) {
          return replaceDrReplicaMutation.mutateAsync({
            primaryUniverseUuid: drConfig.primaryUniverseUuid ?? '',
            drReplicaUniverseUuid: formValues.targetUniverse.value.universeUUID,
            bootstrapParams: {
              backupRequestParams: {
                storageConfigUUID: storageConfigUuid
              }
            }
          });
        }
        return;
      default:
        return assertUnreachableCase(formValues.repairType);
    }
  };

  const universeOptions = universeList
    .filter(
      (universe) =>
        universe.universeUUID !== sourceUniverseUuid &&
        universe.universeUUID !== targetUniverseUuid &&
        !UnavailableUniverseStates.includes(getUniverseStatus(universe).state)
    )
    .map((universe) => {
      return {
        label: universe.name,
        value: universe
      };
    });

  const handleOptionCardClick = (
    repairType: RepairType,
    onChange: (repairType: RepairType) => void
  ) => {
    switch (repairType) {
      case RepairType.USE_EXISITING_TARGET_UNIVERSE:
        onChange(RepairType.USE_EXISITING_TARGET_UNIVERSE);
        return;
      case RepairType.USE_NEW_TARGET_UNIVERSE:
        if (universeOptions.length) {
          onChange(RepairType.USE_NEW_TARGET_UNIVERSE);
        }
        return;
      default:
        assertUnreachableCase(repairType);
    }
  };

  const repairType = formMethods.watch('repairType');
  const isFormDisabled = formMethods.formState.isSubmitting;
  return (
    <YBModal
      title={t('title')}
      submitLabel={t('submitButton')}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      onSubmit={formMethods.handleSubmit(onSubmit)}
      buttonProps={{ primary: { disabled: isFormDisabled } }}
      isSubmitting={formMethods.formState.isSubmitting}
      maxWidth="xl"
      overrideWidth="960px"
      overrideHeight="640px"
      dialogContentProps={{
        className: classes.dialogContentRoot
      }}
      {...modalProps}
    >
      <Typography className={classes.instructionsHeader} variant="h6">
        {t('instructions')}
      </Typography>
      <Controller
        control={formMethods.control}
        name="repairType"
        rules={{ required: t('error.repairTypeRequired') }}
        render={({ field: { onChange } }) => (
          <Box display="flex" gridGap={theme.spacing(1)}>
            <Box width="50%">
              <RbacValidator
                accessRequiredOn={ApiPermissionMap.DR_CONFIG_RESTART}
                isControl
                overrideStyle={{ display: 'unset' }}
              >
                <div
                  className={clsx(
                    classes.optionCard,
                    repairType === RepairType.USE_EXISITING_TARGET_UNIVERSE && classes.selected
                  )}
                  onClick={() =>
                    handleOptionCardClick(RepairType.USE_EXISITING_TARGET_UNIVERSE, onChange)
                  }
                >
                  <div className={classes.optionCardHeader}>
                    <Typography variant="body1">
                      {t('option.useExistingTargetUniverse.optionName')}
                    </Typography>
                    <Box display="flex" alignItems="center" marginLeft="auto">
                      {repairType === RepairType.USE_EXISITING_TARGET_UNIVERSE ? (
                        <SelectedIcon />
                      ) : (
                        <UnselectedIcon />
                      )}
                    </Box>
                  </div>
                  <Typography variant="body2">
                    <Trans
                      i18nKey={`${TRANSLATION_KEY_PREFIX}.option.useExistingTargetUniverse.description`}
                      values={{
                        sourceUniverseName: sourceUniverse.name,
                        targetUniverseName: targetUniverse.name
                      }}
                      components={{ bold: <b /> }}
                    />
                  </Typography>
                </div>
              </RbacValidator>
            </Box>
            <Box width="50%">
              <RbacValidator
                accessRequiredOn={ApiPermissionMap.DR_CONFIG_REPLACE_REPLICA}
                isControl
                overrideStyle={{ display: 'unset' }}
              >
                <div
                  className={clsx(
                    classes.optionCard,
                    repairType === RepairType.USE_NEW_TARGET_UNIVERSE && classes.selected,
                    !universeOptions.length && classes.disabled
                  )}
                  onClick={() =>
                    handleOptionCardClick(RepairType.USE_NEW_TARGET_UNIVERSE, onChange)
                  }
                >
                  <div className={classes.optionCardHeader}>
                    <Typography variant="body1">
                      {t('option.useNewTargetUniverse.optionName')}
                    </Typography>
                    <Box display="flex" alignItems="center" marginLeft="auto">
                      {repairType === RepairType.USE_NEW_TARGET_UNIVERSE ? (
                        <SelectedIcon />
                      ) : (
                        <UnselectedIcon />
                      )}
                    </Box>
                  </div>
                  <Typography variant="body2" className={classes.fieldLabel}>
                    {t('option.useNewTargetUniverse.drReplica')}
                  </Typography>
                  <YBReactSelectField
                    control={formMethods.control}
                    name="targetUniverse"
                    options={universeOptions}
                    rules={{
                      required:
                        repairType === RepairType.USE_NEW_TARGET_UNIVERSE
                          ? t('error.fieldRequired')
                          : false
                    }}
                    isDisabled={isFormDisabled}
                  />
                </div>
              </RbacValidator>
            </Box>
          </Box>
        )}
      />
      {formMethods.formState.errors.repairType?.message && (
        <FormHelperText error={true}>
          {formMethods.formState.errors.repairType.message}
        </FormHelperText>
      )}
      <Box marginTop={3} maxWidth="400px">
        <div className={classes.fieldLabel}>
          <Typography variant="body2">
            {t('option.useExistingTargetUniverse.backupStorageConfig.label')}
          </Typography>
          <YBTooltip
            title={
              <Typography variant="body2">
                <Trans
                  i18nKey={`${TRANSLATION_KEY_PREFIX}.option.useExistingTargetUniverse.backupStorageConfig.tooltip`}
                  components={{ paragraph: <p />, bold: <b /> }}
                />
              </Typography>
            }
          >
            <InfoIcon className={classes.infoIcon} />
          </YBTooltip>
        </div>
        <ReactSelectStorageConfigField
          control={formMethods.control}
          name="storageConfig"
          rules={{ required: t('error.fieldRequired') }}
          isDisabled={isFormDisabled}
        />
      </Box>
    </YBModal>
  );
};

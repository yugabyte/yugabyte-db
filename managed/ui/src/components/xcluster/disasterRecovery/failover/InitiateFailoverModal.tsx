import { useState } from 'react';
import { AxiosError } from 'axios';
import { Box, makeStyles, Typography } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { useSelector } from 'react-redux';

import { YBInput, YBModal, YBModalProps, YBTooltip } from '../../../../redesign/components';
import { api, drConfigQueryKey, universeQueryKey } from '../../../../redesign/helpers/api';
import { fetchTaskUntilItCompletes } from '../../../../actions/xClusterReplication';
import { handleServerError } from '../../../../utils/errorHandlingUtils';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import { getNamespaceIdSafetimeEpochUsMap } from '../utils';
import { isActionFrozen } from '../../../../redesign/helpers/utils';
import { EstimatedDataLossLabel } from '../drConfig/EstimatedDataLossLabel';
import { IStorageConfig as BackupStorageConfig } from '../../../backupv2';
import { AllowedTasks } from '../../../../redesign/helpers/dtos';
import { DrConfig } from '../dtos';
import { UNIVERSE_TASKS } from '../../../../redesign/helpers/constants';

import toastStyles from '../../../../redesign/styles/toastStyles.module.scss';

import InfoIcon from '../../../../redesign/assets/info-message.svg';

interface InitiateFailoverrModalProps {
  drConfig: DrConfig;
  modalProps: YBModalProps;
  allowedTasks: AllowedTasks;
}

const useStyles = makeStyles((theme) => ({
  modalDescription: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2),

    paddingBottom: theme.spacing(4),

    borderBottom: `1px solid ${theme.palette.ybacolors.ybBorderGray}`
  },
  dataLossSection: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1),

    padding: `${theme.spacing(2)}px ${theme.spacing(3)}px`,

    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: '8px'
  },
  propertyLabel: {
    display: 'flex',
    gap: theme.spacing(1),
    alignItems: 'center',

    color: theme.palette.ybacolors.textDarkGray
  },
  infoIcon: {
    '&:hover': {
      cursor: 'pointer'
    }
  },
  rpoBenchmarkIcon: {
    fontSize: '16px'
  },
  confirmTextInputBox: {
    width: '400px'
  },
  success: {
    color: theme.palette.ybacolors.success
  },
  fieldLabel: {
    marginBottom: theme.spacing(1)
  }
}));

const MODAL_NAME = 'InitiateFailoverModal';
const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.failover.initiateModal';

export const InitiateFailoverModal = ({
  drConfig,
  modalProps,
  allowedTasks
}: InitiateFailoverrModalProps) => {
  const [isSubmitting, setIsSubmitting] = useState<boolean>(false);
  const [confirmationText, setConfirmationText] = useState<string>('');
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();
  const queryClient = useQueryClient();

  const currentSafetimesQuery = useQuery(drConfigQueryKey.safetimes(drConfig.uuid), () =>
    api.fetchCurrentSafetimes(drConfig.uuid)
  );
  const storageConfigs: BackupStorageConfig[] = useSelector((reduxState: any) =>
    reduxState?.customer?.configs?.data.filter(
      (storageConfig: BackupStorageConfig) => storageConfig.type === 'STORAGE'
    )
  );
  const storageConfigName =
    storageConfigs?.find(
      (storageConfig) =>
        storageConfig.configUUID === drConfig.bootstrapParams.backupRequestParams.storageConfigUUID
    )?.configName ?? '';

  const targetUniverseUuid = drConfig.drReplicaUniverseUuid;
  const targetUniverseQuery = useQuery(
    universeQueryKey.detail(targetUniverseUuid),
    () => api.fetchUniverse(targetUniverseUuid),
    { enabled: targetUniverseUuid !== undefined }
  );

  const initiateFailoverrMutation = useMutation(
    ({
      drConfig,
      namespaceIdSafetimeEpochUsMap
    }: {
      drConfig: DrConfig;
      namespaceIdSafetimeEpochUsMap: { [namespaceId: string]: string };
    }) =>
      api.initiateFailover(drConfig.uuid, {
        primaryUniverseUuid: drConfig.drReplicaUniverseUuid ?? '',
        drReplicaUniverseUuid: drConfig.primaryUniverseUuid ?? '',
        namespaceIdSafetimeEpochUsMap
      }),
    {
      onSuccess: (response, { drConfig }) => {
        const invalidateQueries = () => {
          queryClient.invalidateQueries(drConfigQueryKey.ALL, { exact: true });
          queryClient.invalidateQueries(drConfigQueryKey.detail(drConfig.uuid));

          // The `drConfigUuidsAsSource` and `drConfigUuidsAsTarget` fields will need to be updated as
          // we switched roles for both universes.
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
                  {t('error.taskFailure')}
                </Typography>
                <a href={`/tasks/${response.taskUUID}`} rel="noopener noreferrer" target="_blank">
                  {t('viewDetails', { keyPrefix: 'task' })}
                </a>
              </span>
            );
          } else {
            toast.success(
              <span className={toastStyles.toastMessage}>
                <Typography variant="body2" component="span">
                  <Trans
                    i18nKey={`${TRANSLATION_KEY_PREFIX}.success.taskSuccess`}
                    components={{
                      universeLink: <a href={`/universes/${drConfig.drReplicaUniverseUuid}`} />,
                      bold: <b />
                    }}
                    values={{ sourceUniverseName: targetUniverseQuery.data?.name }}
                  />
                </Typography>
              </span>
            );
          }
          invalidateQueries();
        };

        modalProps.onClose();
        fetchTaskUntilItCompletes(response.taskUUID, handleTaskCompletion, invalidateQueries);
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, { customErrorLabel: t('error.requestFailureLabel') })
    }
  );

  const modalTitle = t('title');
  const cancelLabel = t('cancel', { keyPrefix: 'common' });
  if (
    !drConfig.primaryUniverseUuid ||
    !drConfig.drReplicaUniverseUuid ||
    targetUniverseQuery.isError ||
    currentSafetimesQuery.isError
  ) {
    const customErrorMessage = !drConfig.primaryUniverseUuid
      ? t('undefinedDrPrimaryUniverseUuid', {
          keyPrefix: 'clusterDetail.disasterRecovery.error'
        })
      : !drConfig.drReplicaUniverseUuid
      ? t('undefinedDrReplicaUniverseUuid', {
          keyPrefix: 'clusterDetail.disasterRecovery.error'
        })
      : targetUniverseQuery.isError
      ? t('failedToFetchDrReplicaUniverse', {
          keyPrefix: 'queryError',
          universeUuid: drConfig.drReplicaUniverseUuid
        })
      : currentSafetimesQuery.isError
      ? t('failedToFetchCurrentSafetimes', {
          keyPrefix: 'clusterDetail.xCluster.error'
        })
      : '';

    return (
      <YBModal
        title={modalTitle}
        cancelLabel={cancelLabel}
        submitTestId={`${MODAL_NAME}-SubmitButton`}
        cancelTestId={`${MODAL_NAME}-CancelButton`}
        size="md"
        {...modalProps}
      >
        <YBErrorIndicator customErrorMessage={customErrorMessage} />
      </YBModal>
    );
  }

  if (
    targetUniverseQuery.isLoading ||
    targetUniverseQuery.isIdle ||
    currentSafetimesQuery.isLoading ||
    currentSafetimesQuery.isIdle
  ) {
    return (
      <YBModal
        title={modalTitle}
        cancelLabel={cancelLabel}
        submitTestId={`${MODAL_NAME}-SubmitButton`}
        cancelTestId={`${MODAL_NAME}-CancelButton`}
        size="md"
        {...modalProps}
      >
        <YBLoading />
      </YBModal>
    );
  }

  const namespaceIdSafetimeEpochUsMap = getNamespaceIdSafetimeEpochUsMap(
    currentSafetimesQuery.data
  );
  const onSubmit = () => {
    setIsSubmitting(true);
    initiateFailoverrMutation.mutate(
      { drConfig, namespaceIdSafetimeEpochUsMap },
      { onSettled: () => resetModal() }
    );
  };
  const resetModal = () => {
    setIsSubmitting(false);
    setConfirmationText('');
  };

  const isFailoverActionFrozen = isActionFrozen(allowedTasks, UNIVERSE_TASKS.FAILOVER_DR);
  const targetUniverseName = targetUniverseQuery.data.name;
  const isFormDisabled =
    isSubmitting || confirmationText !== targetUniverseName || isFailoverActionFrozen;

  return (
    <YBModal
      title={modalTitle}
      cancelLabel={cancelLabel}
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      cancelTestId={`${MODAL_NAME}-CancelButton`}
      submitLabel={t('submitButton')}
      onSubmit={onSubmit}
      buttonProps={{ primary: { disabled: isFormDisabled } }}
      isSubmitting={isSubmitting}
      size="md"
      {...modalProps}
    >
      <div className={classes.modalDescription}>
        <Typography variant="body2">
          <Trans i18nKey={`${TRANSLATION_KEY_PREFIX}.description`} components={{ bold: <b /> }} />
        </Typography>
        <div className={classes.dataLossSection}>
          <div className={classes.propertyLabel}>
            <Typography variant="body1">{t('estimatedDataLoss.label')}</Typography>
            <YBTooltip title={t('estimatedDataLoss.tooltip')}>
              <img src={InfoIcon} alt={t('infoIcon', { keyPrefix: 'imgAltText' })} />
            </YBTooltip>
          </div>
          <div>
            <EstimatedDataLossLabel drConfigUuid={drConfig.uuid} />
          </div>
        </div>
        {storageConfigName ? (
          <Typography variant="body2">
            <Trans
              i18nKey={`clusterDetail.disasterRecovery.backupStorageConfig.currentStorageConfigInfo`}
              components={{ bold: <b /> }}
              values={{ storageConfigName: storageConfigName }}
            />
          </Typography>
        ) : (
          <Typography variant="body2">
            {t('missingStorageConfigInfo', {
              keyPrefix: 'clusterDetail.disasterRecovery.backupStorageConfig'
            })}
          </Typography>
        )}
      </div>
      <Box marginTop={2}>
        <Typography variant="body2">
          <Trans
            i18nKey={`${TRANSLATION_KEY_PREFIX}.failoverConfirmation`}
            values={{ drReplicaName: targetUniverseName }}
            components={{
              bold: <b />
            }}
          />
        </Typography>
      </Box>
      <Box marginTop={3}>
        <Typography variant="body2" className={classes.fieldLabel}>
          {t('confirmationInstructions')}
        </Typography>
        <YBInput
          className={classes.confirmTextInputBox}
          placeholder={targetUniverseName}
          value={confirmationText}
          onChange={(event) => setConfirmationText(event.target.value)}
        />
      </Box>
    </YBModal>
  );
};

import { useState } from 'react';
import { AxiosError } from 'axios';
import { Box, makeStyles, Typography } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { ArrowDropDown } from '@material-ui/icons';
import clsx from 'clsx';

import { ReactComponent as InfoIcon } from '../../../../redesign/assets/info-message.svg';
import { YBInput, YBModal, YBModalProps, YBTooltip } from '../../../../redesign/components';
import { api, drConfigQueryKey, universeQueryKey } from '../../../../redesign/helpers/api';
import { fetchTaskUntilItCompletes } from '../../../../actions/xClusterReplication';
import { handleServerError } from '../../../../utils/errorHandlingUtils';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import { getNamespaceIdSafetimeEpochUsMap } from '../utils';

import { DrConfig } from '../dtos';

import toastStyles from '../../../../redesign/styles/toastStyles.module.scss';

interface InitiateFailoverrModalProps {
  drConfig: DrConfig;
  modalProps: YBModalProps;
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
  success: {
    color: theme.palette.ybacolors.success
  },
  fieldLabel: {
    marginBottom: theme.spacing(1)
  }
}));

const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.failover.initiateModal';

export const InitiateFailoverModal = ({ drConfig, modalProps }: InitiateFailoverrModalProps) => {
  const [isSubmitting, setIsSubmitting] = useState<boolean>(false);
  const [confirmationText, setConfirmationText] = useState<string>('');
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();
  const queryClient = useQueryClient();

  const currentSafetimesQuery = useQuery(drConfigQueryKey.safetimes(drConfig.uuid), () =>
    api.fetchCurrentSafetimes(drConfig.uuid)
  );

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
  if (targetUniverseQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('faliedToFetchTargetuniverse', {
          keyPrefix: 'clusterDetail.xCluster.error'
        })}
      />
    );
  }
  if (currentSafetimesQuery.isError) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('failedToFetchCurrentSafetimes', {
          keyPrefix: 'clusterDetail.xCluster.error'
        })}
      />
    );
  }
  if (
    targetUniverseQuery.isLoading ||
    targetUniverseQuery.isIdle ||
    currentSafetimesQuery.isLoading ||
    currentSafetimesQuery.isIdle
  ) {
    return <YBLoading />;
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

  const targetUniverseName = targetUniverseQuery.data.name;
  const isFormDisabled = isSubmitting || confirmationText !== targetUniverseName;
  return (
    <YBModal
      title={t('title')}
      submitLabel={t('submitButton')}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
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
              <InfoIcon className={classes.infoIcon} />
            </YBTooltip>
          </div>
          <Box display="flex" justifyContent="space-between">
            <Typography variant="body1">Placeholder</Typography>
            <Box display="flex" alignItems="center">
              {/* TODO: Check estimate data loss vs. RPO to determine the icon to show. */}
              <ArrowDropDown className={clsx(classes.rpoBenchmarkIcon, classes.success)} />
              <Typography variant="body2">
                {t('estimatedDataLoss.rpoBenchmark.below', { ms: 100 })}
              </Typography>
            </Box>
          </Box>
        </div>
      </div>
      <Box marginTop={4}>
        <Typography variant="body2" className={classes.fieldLabel}>
          {t('confirmationInstructions')}
        </Typography>
        <YBInput
          fullWidth
          placeholder={targetUniverseName}
          value={confirmationText}
          onChange={(event) => setConfirmationText(event.target.value)}
        />
      </Box>
    </YBModal>
  );
};

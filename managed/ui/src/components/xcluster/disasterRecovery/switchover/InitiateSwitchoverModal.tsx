import { useState } from 'react';
import { AxiosError } from 'axios';
import { Box, makeStyles, Typography } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { useMutation, useQuery, useQueryClient } from 'react-query';

import { YBInput, YBModal, YBModalProps, YBTooltip } from '../../../../redesign/components';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import { YBBanner, YBBannerVariant } from '../../../common/descriptors';
import { api, drConfigQueryKey, universeQueryKey } from '../../../../redesign/helpers/api';
import { fetchTaskUntilItCompletes } from '../../../../actions/xClusterReplication';
import { isActionFrozen } from '../../../../redesign/helpers/utils';
import { handleServerError } from '../../../../utils/errorHandlingUtils';
import { AllowedTasks } from '../../../../redesign/helpers/dtos';
import { DrConfig } from '../dtos';
import { UNIVERSE_TASKS } from '../../../../redesign/helpers/constants';

import toastStyles from '../../../../redesign/styles/toastStyles.module.scss';

import InfoIcon from '../../../../redesign/assets/info-message.svg';

interface InitiateSwitchoverModalProps {
  drConfig: DrConfig;
  modalProps: YBModalProps;
  allowedTasks: AllowedTasks;
}

const useStyles = makeStyles((theme) => ({
  modalTitle: {
    display: 'flex',
    gap: theme.spacing(1),
    alignItems: 'center'
  },
  modalDescription: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2),

    paddingBottom: theme.spacing(4),

    borderBottom: `1px solid ${theme.palette.ybacolors.ybBorderGray}`
  },
  fieldLabel: {
    marginBottom: theme.spacing(1)
  },
  infoIcon: {
    '&:hover': {
      cursor: 'pointer'
    }
  },
  infoBanner: {
    marginTop: 'auto'
  },
  confirmTextInputBox: {
    width: '400px'
  },
  dialogContentRoot: {
    display: 'flex',
    flexDirection: 'column'
  }
}));

const MODAL_NAME = 'InitiateSwitchoverModal';
const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.switchover.initiateModal';

export const InitiateSwitchoverModal = ({
  drConfig,
  modalProps,
  allowedTasks
}: InitiateSwitchoverModalProps) => {
  const [isSubmitting, setIsSubmitting] = useState<boolean>(false);
  const [confirmationText, setConfirmationText] = useState<string>('');
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();
  const queryClient = useQueryClient();

  const targetUniverseUuid = drConfig.drReplicaUniverseUuid;
  const targetUniverseQuery = useQuery(
    universeQueryKey.detail(targetUniverseUuid),
    () => api.fetchUniverse(targetUniverseUuid),
    { enabled: targetUniverseUuid !== undefined }
  );

  const initiateSwitchoverMutation = useMutation(
    (drConfig: DrConfig) =>
      api.initiateSwitchover(drConfig.uuid, {
        primaryUniverseUuid: drConfig.drReplicaUniverseUuid ?? '',
        drReplicaUniverseUuid: drConfig.primaryUniverseUuid ?? ''
      }),
    {
      onSuccess: (response, drConfig) => {
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

  const modalTitle = (
    <Typography variant="h4" component="span" className={classes.modalTitle}>
      {t('title')}
      <YBTooltip
        title={
          <Typography variant="body2">
            <Trans
              i18nKey={`${TRANSLATION_KEY_PREFIX}.titleTooltip`}
              components={{ paragraph: <p /> }}
            />
          </Typography>
        }
      >
        <img src={InfoIcon} alt={t('infoIcon', { keyPrefix: 'imgAltText' })} />
      </YBTooltip>
    </Typography>
  );
  const cancelLabel = t('cancel', { keyPrefix: 'common' });
  if (
    !drConfig.primaryUniverseUuid ||
    !drConfig.drReplicaUniverseUuid ||
    targetUniverseQuery.isError
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
      : '';

    return (
      <YBModal
        customTitle={modalTitle}
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

  if (targetUniverseQuery.isLoading || targetUniverseQuery.isIdle) {
    return <YBLoading />;
  }

  const resetModal = () => {
    setIsSubmitting(false);
    setConfirmationText('');
  };
  const onSubmit = () => {
    setIsSubmitting(true);
    initiateSwitchoverMutation.mutate(drConfig, { onSettled: () => resetModal() });
  };

  const isSwitchoverActionFrozen = isActionFrozen(allowedTasks, UNIVERSE_TASKS.SWITCHIVER_DR);
  const targetUniverseName = targetUniverseQuery.data.name;
  const isFormDisabled =
    isSubmitting || confirmationText !== targetUniverseName || isSwitchoverActionFrozen;

  return (
    <YBModal
      customTitle={modalTitle}
      submitLabel={t('submitButton')}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      onSubmit={onSubmit}
      buttonProps={{ primary: { disabled: isFormDisabled } }}
      isSubmitting={isSubmitting}
      size="md"
      dialogContentProps={{
        className: classes.dialogContentRoot
      }}
      {...modalProps}
    >
      <div className={classes.modalDescription}>
        <Typography variant="body2">
          <Trans i18nKey={`${TRANSLATION_KEY_PREFIX}.instructions`} components={{ bold: <b /> }} />
        </Typography>
        <Typography variant="body2">
          <Trans i18nKey={`${TRANSLATION_KEY_PREFIX}.noDataLoss`} components={{ bold: <b /> }} />
        </Typography>
      </div>
      <Box marginTop={2}>
        <Typography variant="body2">
          <Trans
            i18nKey={`${TRANSLATION_KEY_PREFIX}.switchoverConfirmation`}
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
      <YBBanner className={classes.infoBanner} variant={YBBannerVariant.INFO}>
        <Trans
          i18nKey={`${TRANSLATION_KEY_PREFIX}.note.stopWorkload`}
          components={{ bold: <b /> }}
        />
      </YBBanner>
    </YBModal>
  );
};

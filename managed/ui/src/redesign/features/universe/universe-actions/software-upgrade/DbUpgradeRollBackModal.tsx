import { AxiosError } from 'axios';
import { FormHelperText, makeStyles, Typography } from '@material-ui/core';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { Trans, useTranslation } from 'react-i18next';
import { SubmitHandler, useForm } from 'react-hook-form';

import {
  YBInputField,
  YBModal,
  YBRadio,
  type YBModalProps,
  YBTooltip
} from '@app/redesign/components';
import { universeQueryKey } from '@app/redesign/helpers/api';
import { hasNecessaryPerm } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '@app/redesign/features/rbac/ApiAndUserPermMapping';
import { RBAC_ERR_MSG_NO_PERM } from '@app/redesign/features/rbac/common/validator/ValidatorUtils';
import {
  getUniverse,
  getGetUniverseQueryKey,
  rollbackSoftwareUpgrade
} from '@app/v2/api/universe/universe';
import type { UniverseRollbackUpgradeReqBody } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { handleServerError } from '@app/utils/errorHandlingUtils';
import { formatYbSoftwareVersionString } from '@app/utils/Formatters';
import { UpgradePace } from './constants';
import { getPrimaryCluster } from '@app/redesign/utils/universeUtils';
import { getPlacementAzMetadataList } from './utils/formUtils';

import ClockRewindIcon from '@app/redesign/assets/clock-rewind.svg';
import InfoIcon from '@app/redesign/assets/info-message.svg';
import AlertIcon from '@app/redesign/assets/alert.svg';

const MODAL_NAME = 'DbUpgradeRollBackModal';
const TRANSLATION_KEY_PREFIX = 'universeActions.dbUpgrade.rollbackModal';

export interface DbUpgradeRollBackFormFields {
  rollBackPace: typeof UpgradePace.ROLLING | typeof UpgradePace.CONCURRENT;
  maxNodesPerBatch: number;
  waitBetweenBatchesSeconds: number;
}

interface DbUpgradeRollBackModalProps {
  universeUuid: string;
  modalProps: YBModalProps;
}

const useStyles = makeStyles((theme) => ({
  modalContainer: {
    display: 'flex',
    flexDirection: 'column',

    padding: theme.spacing(2.5),

    overflowY: 'auto'
  },
  confirmationText: {
    color: theme.palette.grey[900],
    fontSize: 13,
    fontWeight: 400,
    lineHeight: '19px'
  },
  confirmationVersion: {
    fontWeight: 700
  },
  rollbackPaceCard: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2),

    marginTop: theme.spacing(2.5),
    padding: theme.spacing(2),

    backgroundColor: theme.palette.common.white,
    border: `1px solid ${theme.palette.grey[300]}`,
    borderRadius: theme.shape.borderRadius
  },
  rollbackPaceTitle: {
    color: theme.palette.grey[700],
    fontSize: '11.5px',
    fontWeight: 500,
    lineHeight: '16px'
  },
  paceOptionsContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1)
  },
  paceOption: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1),

    cursor: 'pointer'
  },
  paceOptionLabel: {
    color: theme.palette.grey[900],
    fontSize: 13,
    fontWeight: 400,
    lineHeight: '16px'
  },
  rollingSettings: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2),

    width: 550,
    maxWidth: '100%',
    padding: theme.spacing(1.5, 2),

    backgroundColor: theme.palette.ybacolors.grey005,
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.shape.borderRadius
  },
  upgradePaceFormFieldContainer: {
    display: 'flex',
    flexDirection: 'column'
  },
  upgradePaceFormField: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1)
  },
  settingLabel: {
    flexShrink: 0,

    color: theme.palette.grey[900],
    fontSize: 13,
    fontWeight: 400,
    lineHeight: '16px'
  },
  numericInputField: {
    flexShrink: 0,

    width: 100,

    '& .MuiInputBase-root': {
      height: 32
    }
  },
  upgradedAzsSection: {
    display: 'flex',
    gap: theme.spacing(1),

    marginTop: theme.spacing(2),
    padding: theme.spacing(1, 2),

    backgroundColor: theme.palette.ybacolors.grey050,
    border: `1px solid ${theme.palette.grey[300]}`,
    borderRadius: theme.shape.borderRadius
  },
  upgradedAzsLabel: {
    marginTop: theme.spacing(0.75),
    width: 90,

    color: theme.palette.grey[700],
    fontSize: '11.5px',
    fontWeight: 500,
    lineHeight: '16px'
  },
  azTagsContainer: {
    display: 'flex',
    flexWrap: 'wrap',
    gap: theme.spacing(1)
  },
  azTag: {
    display: 'inline-flex',
    alignItems: 'center',

    padding: theme.spacing(0.5, 0.75),

    backgroundColor: theme.palette.common.white,
    border: `1px solid ${theme.palette.grey[300]}`,
    borderRadius: 6,
    color: theme.palette.grey[900],
    fontWeight: 400,
    fontSize: '11.5px',
    lineHeight: '16px'
  },
  rollingSettingsWrapper: {
    paddingLeft: theme.spacing(4)
  },
  tooltipIconWrapper: {
    lineHeight: 0
  },
  concurrentRollbackWarningBanner: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1),

    padding: theme.spacing(1),

    backgroundColor: theme.palette.warning[50],
    border: `1px solid ${theme.palette.warning[100]}`,
    borderRadius: theme.shape.borderRadius,
    color: theme.palette.grey[900],
    fontSize: 11.5,
    fontWeight: 400,
    lineHeight: '16px'
  },
  concurrentRollbackWarningIcon: {
    color: theme.palette.warning[700]
  }
}));

export const DbUpgradeRollBackModal = ({
  universeUuid,
  modalProps
}: DbUpgradeRollBackModalProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();
  const queryClient = useQueryClient();

  const universeDetailsQuery = useQuery(universeQueryKey.detailsV2(universeUuid), () =>
    getUniverse(universeUuid)
  );

  const universe = universeDetailsQuery.data;
  const prevVersion = universe?.info?.previous_yb_software_details?.yb_software_version ?? '';
  const maxNodesPerBatchMaximum = universe?.info?.roll_max_batch_size?.primary_batch_size ?? 1;
  const upgradedAzMetadataList =
    getPlacementAzMetadataList(getPrimaryCluster(universe?.spec?.clusters ?? [])) ?? [];
  const upgradedAzDisplayNameByUuid = Object.fromEntries(
    upgradedAzMetadataList.map((az) => [az.azUuid, az.displayName])
  );

  // TODO: YBA backend change required to return AZ upgrade status in the task info response.
  // For now, we assume all AZs are upgraded.
  const upgradedAzDisplayNames = Object.values(upgradedAzDisplayNameByUuid);

  const formMethods = useForm<DbUpgradeRollBackFormFields>({
    defaultValues: {
      rollBackPace: UpgradePace.ROLLING,
      maxNodesPerBatch: 1,
      waitBetweenBatchesSeconds: 180
    },
    mode: 'onChange'
  });

  const rollbackMutation = useMutation(
    (data: UniverseRollbackUpgradeReqBody) => rollbackSoftwareUpgrade(universeUuid, data),
    {
      onSuccess: () => {
        queryClient.invalidateQueries(getGetUniverseQueryKey(universeUuid));
        queryClient.invalidateQueries(universeQueryKey.detailsV2(universeUuid));
        modalProps.onClose();
      },
      onError: (error: Error | AxiosError) =>
        handleServerError(error, {
          customErrorLabel: t('toast.rollbackFailedLabel')
        })
    }
  );

  const onSubmit: SubmitHandler<DbUpgradeRollBackFormFields> = (values) => {
    const isRolling = values.rollBackPace === UpgradePace.ROLLING;
    const payload: UniverseRollbackUpgradeReqBody = {
      rolling_upgrade: isRolling,
      sleep_after_master_restart_millis: values.waitBetweenBatchesSeconds * 1000,
      sleep_after_tserver_restart_millis: values.waitBetweenBatchesSeconds * 1000
    };
    if (isRolling && values.maxNodesPerBatch >= 1) {
      payload.roll_max_batch_size = {
        primary_batch_size: values.maxNodesPerBatch
      };
    }
    return rollbackMutation.mutateAsync(payload);
  };

  const hasRollbackPermission = hasNecessaryPerm({
    onResource: universeUuid,
    ...ApiPermissionMap.UPGRADE_UNIVERSE_ROLLBACK
  });
  const rollbackPace = formMethods.watch('rollBackPace');
  const isFormDisabled = formMethods.formState.isSubmitting;
  const isRollingOptionsDisabled = rollbackPace === UpgradePace.CONCURRENT;

  const handleRollbackPaceChange = (pace: DbUpgradeRollBackFormFields['rollBackPace']) => {
    formMethods.setValue('rollBackPace', pace);
    if (pace === UpgradePace.CONCURRENT) {
      formMethods.clearErrors(['maxNodesPerBatch', 'waitBetweenBatchesSeconds']);
    } else {
      formMethods.trigger(['maxNodesPerBatch', 'waitBetweenBatchesSeconds']);
    }
  };

  return (
    <YBModal
      title={t('modalTitle')}
      titleIcon={<ClockRewindIcon />}
      submitLabel={t('submitLabel')}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      cancelTestId={`${MODAL_NAME}-CancelButton`}
      overrideWidth="700px"
      overrideHeight="fit-content"
      size="md"
      titleSeparator
      dialogContentProps={{ className: classes.modalContainer }}
      onSubmit={formMethods.handleSubmit(onSubmit)}
      isSubmitting={formMethods.formState.isSubmitting}
      hideCloseBtn={false}
      buttonProps={{
        primary: {
          disabled: !hasRollbackPermission || !prevVersion
        }
      }}
      submitButtonTooltip={!hasRollbackPermission ? RBAC_ERR_MSG_NO_PERM : ''}
      {...modalProps}
    >
      <Typography className={classes.confirmationText} component="p">
        {t('confirmationMessage')}{' '}
        <span className={classes.confirmationVersion}>
          {prevVersion ? formatYbSoftwareVersionString(prevVersion) : '-'}?
        </span>
      </Typography>

      <div className={classes.rollbackPaceCard}>
        <Typography className={classes.rollbackPaceTitle}>{t('rollbackPaceTitle')}</Typography>

        <div className={classes.paceOptionsContainer}>
          <div
            className={classes.paceOption}
            onClick={() => !isFormDisabled && handleRollbackPaceChange(UpgradePace.ROLLING)}
            role="button"
            tabIndex={0}
            onKeyDown={(event) => {
              if ((event.key === 'Enter' || event.key === ' ') && !isFormDisabled) {
                handleRollbackPaceChange(UpgradePace.ROLLING);
              }
            }}
          >
            <YBRadio checked={rollbackPace === UpgradePace.ROLLING} disabled={isFormDisabled} />
            <Typography className={classes.paceOptionLabel}>{t('rollbackPaceRolling')}</Typography>
          </div>
          <div className={classes.rollingSettingsWrapper}>
            <div className={classes.rollingSettings}>
              <div className={classes.upgradePaceFormFieldContainer}>
                <div className={classes.upgradePaceFormField}>
                  <Typography className={classes.settingLabel}>
                    {t('rollingOptions.maxNodesPerBatch')}
                  </Typography>
                  <YBInputField
                    control={formMethods.control}
                    name="maxNodesPerBatch"
                    type="number"
                    className={classes.numericInputField}
                    disabled={
                      isFormDisabled || maxNodesPerBatchMaximum <= 1 || isRollingOptionsDisabled
                    }
                    hideInlineError
                    rules={{
                      validate: (value: unknown) => {
                        if (formMethods.getValues('rollBackPace') !== UpgradePace.ROLLING) {
                          return true;
                        }
                        if (value === undefined || value === null || value === '') {
                          return t('formFieldRequired', { keyPrefix: 'common' });
                        }
                        const num = Number(value);
                        if (num < 1) {
                          return t('validation.maxNodesPerBatchMinimum');
                        }
                        if (num > maxNodesPerBatchMaximum) {
                          return t('validation.maxNodesPerBatchMaximum', {
                            max: maxNodesPerBatchMaximum
                          });
                        }
                        return true;
                      }
                    }}
                    inputProps={{
                      min: 1,
                      max: maxNodesPerBatchMaximum,
                      'data-testid': `${MODAL_NAME}-MaxBatchInput`
                    }}
                  />
                  <YBTooltip
                    title={
                      <Typography
                        variant="body2"
                        component="span"
                        style={{ whiteSpace: 'pre-line' }}
                      >
                        {t('rollingOptions.maxNodesPerBatchTooltip')}
                      </Typography>
                    }
                  >
                    <span className={classes.tooltipIconWrapper}>
                      <InfoIcon width={18} height={18} />
                    </span>
                  </YBTooltip>
                </div>
                {formMethods.formState.errors.maxNodesPerBatch && (
                  <FormHelperText error={true}>
                    {formMethods.formState.errors.maxNodesPerBatch.message}
                  </FormHelperText>
                )}
              </div>
              <div className={classes.upgradePaceFormFieldContainer}>
                <div className={classes.upgradePaceFormField}>
                  <Typography className={classes.settingLabel}>
                    {t('rollingOptions.waitBetweenBatches')}
                  </Typography>
                  <YBInputField
                    control={formMethods.control}
                    name="waitBetweenBatchesSeconds"
                    type="number"
                    className={classes.numericInputField}
                    disabled={isFormDisabled || isRollingOptionsDisabled}
                    hideInlineError
                    rules={{
                      validate: (value: unknown) => {
                        if (formMethods.getValues('rollBackPace') !== UpgradePace.ROLLING) {
                          return true;
                        }
                        if (value === undefined || value === null || value === '') {
                          return t('formFieldRequired', { keyPrefix: 'common' });
                        }
                        const num = Number(value);
                        if (num < 0) {
                          return t('validation.waitBetweenBatchesMin');
                        }
                        return true;
                      }
                    }}
                    inputProps={{
                      min: 0,
                      'data-testid': `${MODAL_NAME}-WaitInput`
                    }}
                  />
                  <Typography className={classes.settingLabel}>
                    {t('seconds', { keyPrefix: 'common' })}
                  </Typography>
                  <YBTooltip
                    title={
                      <Typography
                        variant="body2"
                        component="span"
                        style={{ whiteSpace: 'pre-line' }}
                      >
                        {t('rollingOptions.waitBetweenBatchesTooltip')}
                      </Typography>
                    }
                  >
                    <span className={classes.tooltipIconWrapper}>
                      <InfoIcon width={18} height={18} />
                    </span>
                  </YBTooltip>
                </div>
                {formMethods.formState.errors.waitBetweenBatchesSeconds && (
                  <FormHelperText error={true}>
                    {formMethods.formState.errors.waitBetweenBatchesSeconds.message}
                  </FormHelperText>
                )}
              </div>
            </div>
          </div>
          <div
            className={classes.paceOption}
            onClick={() => !isFormDisabled && handleRollbackPaceChange(UpgradePace.CONCURRENT)}
            role="button"
            tabIndex={0}
            onKeyDown={(event) => {
              if ((event.key === 'Enter' || event.key === ' ') && !isFormDisabled) {
                handleRollbackPaceChange(UpgradePace.CONCURRENT);
              }
            }}
          >
            <YBRadio checked={rollbackPace === UpgradePace.CONCURRENT} disabled={isFormDisabled} />
            <Typography className={classes.paceOptionLabel}>
              {t('rollbackPaceConcurrent')}
            </Typography>
          </div>
          {rollbackPace === UpgradePace.CONCURRENT && (
            <div
              className={classes.concurrentRollbackWarningBanner}
              role="alert"
              data-testid={`${MODAL_NAME}-ConcurrentDowntimeWarning`}
            >
              <AlertIcon className={classes.concurrentRollbackWarningIcon} width={24} height={24} />
              <Typography variant="subtitle1" component="span">
                <Trans
                  t={t}
                  i18nKey="concurrentRollbackDowntimeWarning"
                  components={{ bold: <Typography variant="subtitle2" component="span" /> }}
                />
              </Typography>
            </div>
          )}
        </div>
      </div>

      {upgradedAzDisplayNames.length > 0 && (
        <div className={classes.upgradedAzsSection}>
          <Typography className={classes.upgradedAzsLabel}>{t('upgradedAzsLabel')}</Typography>
          <div className={classes.azTagsContainer}>
            {upgradedAzDisplayNames.map((name) => (
              <span key={name} className={classes.azTag}>
                {name}
              </span>
            ))}
          </div>
        </div>
      )}
    </YBModal>
  );
};

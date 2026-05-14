import { makeStyles, Typography } from '@material-ui/core';
import { AxiosError } from 'axios';
import { SubmitHandler, useForm } from 'react-hook-form';
import { Trans, useTranslation } from 'react-i18next';
import { useMutation, useQuery } from 'react-query';
import { toast } from 'react-toastify';

import { YBModal, YBRadio, type YBModalProps } from '@app/redesign/components';
import { ApiPermissionMap } from '@app/redesign/features/rbac/ApiAndUserPermMapping';
import { hasNecessaryPerm } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { RBAC_ERR_MSG_NO_PERM } from '@app/redesign/features/rbac/common/validator/ValidatorUtils';
import { AZUpgradeStatus, TaskState, TaskType } from '@app/redesign/features/tasks/dtos';
import { useRefreshUniverseTasksCache } from '@app/redesign/helpers/cacheUtils';
import { api, taskQueryKey, universeQueryKey } from '@app/redesign/helpers/api';
import { SortDirection } from '@app/redesign/utils/dtos';
import { handleServerError } from '@app/utils/errorHandlingUtils';
import { formatYbSoftwareVersionString } from '@app/utils/Formatters';
import { getUniverse, rollbackSoftwareUpgrade } from '@app/v2/api/universe/universe';
import type {
  UniverseRollbackUpgradeReqBody,
  YBATaskRespResponse
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { RollingUpdateBatchSettings } from './components/RollingUpdateBatchSettings';
import { UpgradePace } from './constants';
import { getPlacementAzDisplayNameForCluster } from './utils/formUtils';
import { getTaskSoftwareUpgradeProgress } from './upgrade-management/utils';
import { fetchTaskUntilItCompletes } from '@app/actions/xClusterReplication';

import AlertIcon from '@app/redesign/assets/alert.svg';
import ClockRewindIcon from '@app/redesign/assets/clock-rewind.svg';

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
  const refreshUniverseTasksCache = useRefreshUniverseTasksCache(universeUuid);

  const isModalOpen = !!modalProps.open;

  const universeDetailsQuery = useQuery(
    universeQueryKey.detailsV2(universeUuid),
    () => getUniverse(universeUuid),
    { enabled: isModalOpen && !!universeUuid }
  );
  const getPagedSoftwareUpgradeTasksRequest = {
    direction: SortDirection.DESC,
    filter: {
      typeList: [TaskType.SOFTWARE_UPGRADE],
      targetUUIDList: [universeUuid]
    }
  };
  const softwareUpgradeTasksQuery = useQuery(
    taskQueryKey.paged(getPagedSoftwareUpgradeTasksRequest),
    () => api.fetchPagedCustomerTasks(getPagedSoftwareUpgradeTasksRequest),
    { enabled: isModalOpen && !!universeUuid }
  );
  const latestSoftwareUpgradeTask = softwareUpgradeTasksQuery.data?.entities[0];

  const universe = universeDetailsQuery.data;
  const prevVersion = universe?.info?.previous_yb_software_details?.yb_software_version ?? '';
  const maxNodesPerBatchMaximum = universe?.info?.roll_max_batch_size?.primary_batch_size ?? 1;
  const clusters = universe?.spec?.clusters ?? [];

  const upgradedAzs =
    getTaskSoftwareUpgradeProgress(latestSoftwareUpgradeTask)
      ?.tserverAZUpgradeStatesList?.filter((az) => az.status === AZUpgradeStatus.COMPLETED)
      .map((az) => ({
        azUuid: az.azUUID,
        displayName: getPlacementAzDisplayNameForCluster(
          clusters,
          az.clusterUUID,
          az.azUUID,
          az.azName ?? az.azUUID
        )
      })) ?? [];

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
      onSuccess: (response: YBATaskRespResponse) => {
        const handleTaskCompletion = (error: boolean) => {
          if (!error) {
            toast.success(<Typography variant="body2">{t('toast.rollbackSuccess')}</Typography>);
          }
        };

        refreshUniverseTasksCache();
        modalProps.onClose();
        if (response.task_uuid) {
          fetchTaskUntilItCompletes(response.task_uuid, handleTaskCompletion);
        }
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
  const isFormDisabled = formMethods.formState.isSubmitting || !hasRollbackPermission;
  const isRollingOptionsDisabled = rollbackPace === UpgradePace.CONCURRENT;
  const isDbUpgradeTaskCompleted = latestSoftwareUpgradeTask?.status === TaskState.SUCCESS;

  const handleRollbackPaceChange = (pace: DbUpgradeRollBackFormFields['rollBackPace']) => {
    formMethods.setValue('rollBackPace', pace);
    if (pace === UpgradePace.CONCURRENT) {
      formMethods.clearErrors(['maxNodesPerBatch', 'waitBetweenBatchesSeconds']);
    } else {
      void formMethods.trigger(['maxNodesPerBatch', 'waitBetweenBatchesSeconds']);
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
          disabled: isFormDisabled
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
            <YBRadio
              checked={rollbackPace === UpgradePace.ROLLING}
              disabled={isFormDisabled}
              inputProps={{
                'data-testid': `${MODAL_NAME}-RollingRadio`
              }}
            />
            <Typography className={classes.paceOptionLabel}>
              {t(
                `rollbackPaceRolling.${isDbUpgradeTaskCompleted ? 'postUpgrade' : 'upgradeInProgress'}`
              )}
            </Typography>
          </div>
          <div className={classes.rollingSettingsWrapper}>
            <RollingUpdateBatchSettings<DbUpgradeRollBackFormFields>
              control={formMethods.control}
              errors={formMethods.formState.errors}
              maxNodesPerBatchName="maxNodesPerBatch"
              waitBetweenBatchesName="waitBetweenBatchesSeconds"
              maxNodesPerBatchMaximum={maxNodesPerBatchMaximum}
              shouldValidate={(formValues) => formValues.rollBackPace === UpgradePace.ROLLING}
              isRollbackFlow={true}
              isDisabled={isFormDisabled || isRollingOptionsDisabled}
              testIdPrefix={MODAL_NAME}
            />
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
            <YBRadio
              checked={rollbackPace === UpgradePace.CONCURRENT}
              disabled={isFormDisabled}
              inputProps={{
                'data-testid': `${MODAL_NAME}-ConcurrentRadio`
              }}
            />
            <Typography className={classes.paceOptionLabel}>
              {t(
                `rollbackPaceConcurrent.${isDbUpgradeTaskCompleted ? 'postUpgrade' : 'upgradeInProgress'}`
              )}
            </Typography>
          </div>
          {rollbackPace === UpgradePace.CONCURRENT && (
            <div
              className={classes.concurrentRollbackWarningBanner}
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

      {upgradedAzs.length > 0 && !isDbUpgradeTaskCompleted && (
        <div className={classes.upgradedAzsSection}>
          <Typography className={classes.upgradedAzsLabel}>{t('upgradedAzsLabel')}</Typography>
          <div className={classes.azTagsContainer}>
            {upgradedAzs.map(({ azUuid, displayName }) => (
              <span key={azUuid} className={classes.azTag}>
                {displayName}
              </span>
            ))}
          </div>
        </div>
      )}
    </YBModal>
  );
};

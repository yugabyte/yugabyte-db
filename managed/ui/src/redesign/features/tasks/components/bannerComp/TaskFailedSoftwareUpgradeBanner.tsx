/*
 * Created on Wed Jan 29 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';
import { useMutation } from 'react-query';
import { useDispatch, useSelector } from 'react-redux';
import { toast } from 'react-toastify';
import { useToggle } from 'react-use';
import { Typography, makeStyles } from '@material-ui/core';
import { fetchUniverseInfo, fetchUniverseInfoResponse } from '../../../../../actions/universe';
import { AlertVariant, YBAlert, YBButton } from '../../../../components';
import { DBRollbackModal } from '../../../universe/universe-actions/rollback-upgrade/DBRollbackModal';
import { YBProgress, YBProgressBarState } from '../../../../components/YBProgress/YBLinearProgress';
import { RetryConfirmModal } from '../drawerComp/TaskDetailActions';
import { useBannerCommonStyles } from './BannerStyles';
import { useRefetchTasks } from '../../TaskUtils';
import { retryTasks } from '../drawerComp/api';
import { TaskBannerCompProps } from './dtos';
import ErrorIcon from '../../../../assets/error.svg?img';

const useStyles = makeStyles((theme) => ({
  root: {
    padding: '8px 20px',
    background: theme.palette.common.white
  },
  bannerStyles: {
    border: '1px solid rgba(231, 62, 54, 0.25)',
    background: 'rgba(231, 62, 54, 0.15)',
    padding: '8px 16px',
    alignItems: 'flex-start',
    '&>span': {
      width: '100%'
    }
  },
  info: {
    height: '24px',
    marginBottom: '8px'
  },
  percentText: {
    marginLeft: '8px'
  },
  errorIcon: {
    width: 22,
    height: 22
  },
  subText: {
    display: 'flex',
    justifyContent: 'space-between',
    alignItems: 'center'
  },
  actions: {
    display: 'flex',
    gap: '16px',
    justifyContent: 'space-between'
  }
}));

export const TaskFailedSoftwareUpgradeBanner: FC<TaskBannerCompProps> = ({
  currentTask,
  viewDetails
}) => {
  const classes = useStyles();
  const commonStyles = useBannerCommonStyles();
  const [showRetryConfirmationModal, toggleRetryConfirmationModal] = useToggle(false);
  const [openRollbackModal, setRollBackModal] = useToggle(false);
  const refetchTasks = useRefetchTasks();

  const universeData = useSelector((data: any) => data.universe?.currentUniverse?.data);

  const { t } = useTranslation('translation', {
    keyPrefix: 'taskDetails.banner'
  });

  const dispatch = useDispatch();

  // we should refresh the universe info after retrying the task, else the old task banner will be shown
  const refreshUniverse = () => {
    return dispatch(fetchUniverseInfo(currentTask.targetUUID) as any).then((response: any) => {
      return dispatch(fetchUniverseInfoResponse(response.payload));
    });
  };
  const doRetryTask = useMutation(() => retryTasks(currentTask?.id), {
    onSuccess: () => {
      toast.success(t('messages.taskRetrySuccess', { keyPrefix: 'taskDetails.actions' }));
    },
    onError: () => {
      toast.error(t('messages.taskRetryFailed', { keyPrefix: 'taskDetails.actions' }));
    },
    onSettled: () => {
      toggleRetryConfirmationModal(false);
      refreshUniverse();
      refetchTasks();
    }
  });

  return (
    <div className={classes.root}>
      <YBAlert
        text={
          <div>
            <div className={clsx(commonStyles.flex, classes.info)}>
              <Typography variant="body1">{t('softwareUpgradeFailed')}</Typography>
              <Typography variant="subtitle2" className={classes.percentText}>
                {currentTask.percentComplete}%
              </Typography>
              <YBProgress
                state={YBProgressBarState.Error}
                value={currentTask.percentComplete}
                height={8}
                width={130}
              />
            </div>
            <div className={classes.subText}>
              <div>{t('softwareUpgradeFailedText')}</div>
              <div className={classes.actions}>
                <YBButton
                  variant="secondary"
                  onClick={() => viewDetails()}
                  data-testid="failed-software-upgrade-view-details"
                >
                  {t('viewDetails', { keyPrefix: 'taskDetails.simple' })}
                </YBButton>
                <YBButton
                  variant="secondary"
                  onClick={() => setRollBackModal(true)}
                  data-testid="failed-software-upgrade-rollback"
                >
                  {t('rollbackUpgrade')}
                </YBButton>
                <YBButton
                  variant="primary"
                  onClick={() => toggleRetryConfirmationModal(true)}
                  data-testid="failed-software-upgrade-retry"
                >
                  {t('retry', { keyPrefix: 'taskDetails.actions' })}
                </YBButton>
              </div>
            </div>
          </div>
        }
        open
        variant={AlertVariant.Error}
        className={classes.bannerStyles}
        icon={<img alt="" src={ErrorIcon} className={classes.errorIcon} />}
      />
      <RetryConfirmModal
        visible={showRetryConfirmationModal}
        onClose={() => toggleRetryConfirmationModal(false)}
        onSubmit={() => doRetryTask.mutate()}
      />
      <DBRollbackModal
        open={openRollbackModal}
        onClose={() => {
          setRollBackModal(false);
        }}
        universeData={universeData}
      />
    </div>
  );
};

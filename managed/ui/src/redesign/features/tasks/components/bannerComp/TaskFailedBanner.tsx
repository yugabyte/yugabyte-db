/*
 * Created on Fri Dec 22 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';
import { Divider, Typography, makeStyles } from '@material-ui/core';
import { AlertVariant, YBAlert, YBButton } from '../../../../components';
import { YBProgress, YBProgressBarState } from '../../../../components/YBProgress/YBLinearProgress';
import { TaskBannerCompProps } from './dtos';
import { useBannerCommonStyles } from './BannerStyles';
import ErrorIcon from '../../../../assets/error.svg';

const useStyles = makeStyles((theme) => ({
  root: {
    margin: '8px 20px'
  },
  bannerStyles: {
    border: '1px solid rgba(231, 62, 54, 0.25)',
    background: 'rgba(231, 62, 54, 0.15)',
    padding: '8px 16px',
    alignItems: 'center'
  },
  errorIcon: {
    width: 22,
    height: 22
  }
}));

export const TaskFailedBanner: FC<TaskBannerCompProps> = ({
  currentTask,
  viewDetails,
  onClose
}) => {
  const classes = useStyles();
  const commonStyles = useBannerCommonStyles();
  const { t } = useTranslation('translation', {
    keyPrefix: 'taskDetails.banner'
  });
  return (
    <div className={classes.root}>
      <YBAlert
        text={
          <div className={clsx(commonStyles.flex)}>
            <div className={commonStyles.flex}>
              <Typography variant="body1">{currentTask.title}</Typography>
            </div>
            <div className={commonStyles.flex}>
              <Typography variant="body1">
                {t('taskProgress', { percent: Math.trunc(currentTask.percentComplete) })}
              </Typography>
              <YBProgress
                state={YBProgressBarState.Error}
                value={currentTask.percentComplete}
                height={8}
                width={130}
              />
            </div>
            <Divider orientation="vertical" className={commonStyles.divider} />
            <YBButton
              className={commonStyles.viewDetsailsButton}
              variant="secondary"
              size="small"
              onClick={() => viewDetails()}
            >
              {t('viewDetails')}
            </YBButton>
          </div>
        }
        open
        variant={AlertVariant.Error}
        className={classes.bannerStyles}
        onClose={onClose}
        icon={<img alt="" src={ErrorIcon} className={classes.errorIcon} />}
      />
    </div>
  );
};

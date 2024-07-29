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
import { useBannerCommonStyles } from './BannerStyles';
import { TaskBannerCompProps } from './dtos';

const useStyles = makeStyles(() => ({
  root: {
    margin: '8px 20px'
  },
  bannerStyles: {
    border: `1px solid #A5BDF2`,
    background: `#EBF1FF`,
    alignItems: 'center',
    padding: '8px 16px',
    '& svg': {
      marginTop: 0
    }
  },

  content: {
    gap: '12px'
  }
}));

export const TaskInProgressBanner: FC<TaskBannerCompProps> = ({ currentTask, viewDetails }) => {
  const classes = useStyles();
  const commonStyles = useBannerCommonStyles();

  const { t } = useTranslation('translation', {
    keyPrefix: 'taskDetails.banner'
  });

  return (
    <div className={classes.root}>
      <YBAlert
        text={
          <div className={clsx(commonStyles.flex, classes.content)}>
            <div className={commonStyles.flex}>
              <Typography variant="body1">{currentTask.title}...</Typography>
              <Typography variant="body2">{t('opNotAvailable')}</Typography>
            </div>
            <div className={commonStyles.flex}>
              <Typography variant="body1">
                {t('taskProgress', { percent: Math.trunc(currentTask.percentComplete) })}
              </Typography>
              <YBProgress
                state={YBProgressBarState.InProgress}
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
        variant={AlertVariant.InProgress}
        className={classes.bannerStyles}
      />
    </div>
  );
};

/*
 * Created on Mon Jun 10 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { Grid, Typography, makeStyles } from '@material-ui/core';

import { Task } from '../../dtos';
import DiffBadge from './DiffBadge';
import { AlertVariant, YBAlert } from '../../../../components';
import { isTaskFailed, isTaskRunning } from '../../TaskUtils';
import { DiffOperation } from './dtos';

interface TaskDiffBannerProps {
  task: Task;
  diffCount?: number;
}

export const TaskDiffBanner: FC<TaskDiffBannerProps> = ({ task, diffCount }) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'taskDetails.diffModal.alerts'
  });

  if (isTaskRunning(task)) {
    return (
      <YBAlert
        variant={AlertVariant.Info}
        text={t('inProgress', { count: diffCount })}
        open={true}
      />
    );
  }
  if (isTaskFailed(task)) {
    return (
      <YBAlert
        variant={AlertVariant.Warning}
        text={<Trans i18nKey="taskDetails.diffModal.alerts.failed" components={{ b: <b /> }} />}
        open={true}
      />
    );
  }
  return null;
};

interface DiffTitleBannerProps {
  title: string;
  showLegends?: boolean;
}

const useStyles = makeStyles((theme) => ({
  legends: {
    display: 'flex',
    gap: '12px',
    '& > div': {
      display: 'flex',
      alignItems: 'center'
    }
  },
  divider: {
    width: '1px',
    height: '16px',
    borderLeft: `1px solid ${theme.palette.ybacolors.backgroundGrayDark}`,
    marginRight: '12px'
  },
  title: {
    textTransform: 'uppercase',
    color: theme.palette.ybacolors.textDarkGray,
    fontWeight: 500
  }
}));

export const DiffTitleBanner: FC<DiffTitleBannerProps> = ({ title, showLegends = true }) => {
  const classes = useStyles();
  return (
    <Grid justifyContent="space-between" alignItems="center" container>
      <Grid item md={6} lg={6} xs={6}>
        <Typography variant="subtitle1" className={classes.title}>
          {title}
        </Typography>
      </Grid>
      {showLegends && (
        <Grid item className={classes.legends}>
          <div>
            <DiffBadge type={DiffOperation.ADDED} />
          </div>
          <div>
            <span className={classes.divider}></span>
            <DiffBadge type={DiffOperation.CHANGED} />
          </div>
          <div>
            <span className={classes.divider}></span>
            <DiffBadge type={DiffOperation.REMOVED} />
          </div>
        </Grid>
      )}
    </Grid>
  );
};

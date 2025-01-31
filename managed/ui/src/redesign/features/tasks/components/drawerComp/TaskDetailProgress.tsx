/*
 * Created on Wed Dec 20 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Typography, makeStyles } from '@material-ui/core';
import { YBProgress, YBProgressBarState } from '../../../../components/YBProgress/YBLinearProgress';
import { TaskDrawerCompProps } from './dtos';
import { TaskStates } from '../../dtos';

const useStyles = makeStyles(() => ({
  title: {
    fontWeight: 500,
    marginLeft: '2px',
    marginBottom: '16px'
  },
  percentText: {
    textAlign: 'right',
    color: 'rgba(35, 35, 41, 0.40)',
    marginTop: '8px'
  }
}));
export const TaskDetailProgress: FC<TaskDrawerCompProps> = ({ currentTask }) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'taskDetails.progress'
  });

  const classes = useStyles();

  if (!currentTask) return null;

  const getProgressState = () => {
    switch (currentTask.status) {
      case TaskStates.ABORT:
      case TaskStates.ABORTED:
        return YBProgressBarState.Warning;
      case TaskStates.FAILURE:
        return YBProgressBarState.Error;
      case TaskStates.INITIALIZING:
      case TaskStates.CREATED:
      case TaskStates.RUNNING:
        return YBProgressBarState.InProgress;
      case TaskStates.SUCCESS:
        return YBProgressBarState.Success;
      case TaskStates.UNKNOWN:
      default:
        return YBProgressBarState.Unknown;
    }
  };

  return (
    <div>
      <Typography variant="h5" className={classes.title}>
        {t('title')}
      </Typography>
      <YBProgress value={currentTask.percentComplete} state={getProgressState()} height={8} />
      <Typography variant="subtitle1" className={classes.percentText}>
        {t('complete', { progress: Math.trunc(currentTask.percentComplete) })}
      </Typography>
    </div>
  );
};

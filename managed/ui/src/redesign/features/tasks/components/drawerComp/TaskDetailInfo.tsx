/*
 * Created on Wed Dec 20 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, Fragment } from 'react';
import { useTranslation } from 'react-i18next';
import { Typography, makeStyles } from '@material-ui/core';
import { ybFormatDate } from '../../../../helpers/DateUtils';
import { Badge_Types, StatusBadge } from '../../../../../components/common/badge/StatusBadge';
import { TaskDrawerCompProps } from './dtos';
import { Task, TaskStates } from '../../dtos';

const useStyles = makeStyles((theme) => ({
  detailsInfo: {
    borderRadius: '4px',
    border: `1px solid #E3E3E5`,
    background: theme.palette.ybacolors.backgroundGrayLight,
    display: 'flex',
    justifyContent: 'space-between',
    flexWrap: 'wrap'
  },
  info: {
    padding: '16px',
    height: '75px'
  },
  label: {
    marginBottom: '8px',
    color: '#333',
    fontSize: '12px'
  },
  value: {
    color: '#818182',
    fontSize: '12px'
  },
  hr: {
    width: '100%',
    height: '1px',
    background: '#E3E3E5'
  }
}));

type TaskInfo = {
  label: string;
  value: string | JSX.Element;
};

const getBadgeType = (status: Task['status']): Badge_Types => {
  switch (status) {
    case TaskStates.CREATED:
    case TaskStates.INITIALIZING:
    case TaskStates.RUNNING:
      return Badge_Types.IN_PROGRESS;
    case TaskStates.UNKNOWN:
      return Badge_Types.SKIPPED;
    case TaskStates.SUCCESS:
      return Badge_Types.SUCCESS;
    case TaskStates.FAILURE:
      return Badge_Types.FAILED;
    case TaskStates.ABORTED:
    case TaskStates.ABORT:
      return Badge_Types.STOPPED;
  }
};

export const TaskDetailInfo: FC<TaskDrawerCompProps> = ({ currentTask }) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'taskDetails.info'
  });

  const classes = useStyles();

  if (!currentTask) return null;

  const taskInfo: TaskInfo[] = [
    {
      label: t('type'),
      value: currentTask.typeName
    },
    {
      label: t('target'),
      value: currentTask.target
    },
    {
      label: t('status'),
      value: (
        <StatusBadge
          customLabel={t(currentTask.status)}
          statusType={getBadgeType(currentTask.status)}
        />
      )
    },
    {
      label: t('startTime'),
      value: ybFormatDate(currentTask.createTime)
    },
    {
      label: t('endTime'),
      value: ybFormatDate(currentTask.completionTime)
    }
  ];

  if (currentTask.type === 'SoftwareUpgrade') {
    taskInfo.push({
      label: t('startVersion'),
      value: currentTask.details.versionNumbers?.ybSoftwareVersion ?? '-'
    });
    taskInfo.push({
      label: t('targetVersion'),
      value: currentTask.details.versionNumbers?.ybSoftwareVersion ?? '-'
    });
  }

  return (
    <div className={classes.detailsInfo}>
      {taskInfo.map((task, i) => (
        <Fragment key={i}>
          <div className={classes.info} key={i}>
            <Typography variant="subtitle1" className={classes.label}>
              {task.label}
            </Typography>
            <Typography variant="subtitle1" className={classes.value}>
              {task.value}
            </Typography>
          </div>
          {(i + 1) % 3 === 0 && <div className={classes.hr} key={`div${i}`} />}
        </Fragment>
      ))}
    </div>
  );
};

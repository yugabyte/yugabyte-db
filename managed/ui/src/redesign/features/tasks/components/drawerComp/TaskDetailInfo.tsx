/*
 * Created on Wed Dec 20 2023
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, Fragment } from 'react';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';
import { Typography, makeStyles } from '@material-ui/core';
import { isTaskRunning } from '../../TaskUtils';
import { fetchRootSubTaskDetails, getAuditLog } from '../diffComp/api';
import { ybFormatDate } from '../../../../helpers/DateUtils';
import { Badge_Types, StatusBadge } from '../../../../../components/common/badge/StatusBadge';
import { TaskDrawerCompProps } from './dtos';
import { Task, TaskState, TaskType } from '../../dtos';

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
  },
  ellipsis: {
    textOverflow: 'ellipsis',
    whiteSpace: 'nowrap',
    overflow: 'hidden',
    maxWidth: '200px',
    display: 'inline-block'
  },
  rollingRestart: {
    width: '220px'
  }
}));

type TaskInfo = {
  label: string;
  value: string | JSX.Element;
  isHr?: boolean;
  className?: string;
};

const getBadgeType = (status: Task['status']): Badge_Types => {
  switch (status) {
    case TaskState.CREATED:
    case TaskState.INITIALIZING:
    case TaskState.RUNNING:
      return Badge_Types.IN_PROGRESS;
    case TaskState.UNKNOWN:
      return Badge_Types.SKIPPED;
    case TaskState.SUCCESS:
      return Badge_Types.SUCCESS;
    case TaskState.FAILURE:
      return Badge_Types.FAILED;
    case TaskState.ABORTED:
    case TaskState.ABORT:
      return Badge_Types.STOPPED;
  }
};

export const TaskDetailInfo: FC<TaskDrawerCompProps> = ({ currentTask }) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'taskDetails.info'
  });

  const classes = useStyles();

  const isGFLAGTask = currentTask?.type === TaskType.GFlags_UPGRADE;

  const { data: taskDetails, isSuccess, isLoading: isSubTaskDetailsLoading } = useQuery(
    ['subTasks-parent', currentTask!.id!],
    () => fetchRootSubTaskDetails(currentTask!.id!, currentTask!.targetUUID),
    {
      select: (data) => data,
      enabled: !!currentTask && isGFLAGTask
    }
  );

  const { data: auditData, isLoading: isAuditQueryLoading } = useQuery(
    ['auditData', currentTask?.id, isSuccess],
    // if it is a retried task, get the parent task uuid and get it's Audit log
    () =>
      getAuditLog(
        (taskDetails && taskDetails[currentTask!.targetUUID!]?.[0]?.id) ?? currentTask!.id!
      ),
    {
      enabled: !!currentTask && isSuccess && isGFLAGTask,
      select: (data) => data.data
    }
  );

  if (!currentTask) return null;

  const taskInfo: TaskInfo[] = [
    {
      label: t('type'),
      value: (
        <span title={`${currentTask.typeName} ${currentTask.target}`} className={classes.ellipsis}>
          {`${currentTask.typeName} ${currentTask.target}`}
        </span>
      )
    },
    {
      label: t('target'),
      value: (
        <span title={currentTask.title.replace(/.*:\s*/, '')} className={classes.ellipsis}>
          {currentTask.title.replace(/.*:\s*/, '')}
        </span>
      )
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
      label: '',
      value: '',
      isHr: true
    },
    {
      label: t('startTime'),
      value: ybFormatDate(currentTask.createTime)
    },
    {
      label: t('endTime'),
      value: isTaskRunning(currentTask) ? '-' : ybFormatDate(currentTask.completionTime)
    }
  ];

  if (isGFLAGTask) {
    const isLoading = isSubTaskDetailsLoading || isAuditQueryLoading;
    taskInfo.push({
      label: t('upgradeMethod'),
      value: isLoading ? '-' : (auditData?.payload as any)?.upgradeOption
    });
    if ((auditData?.payload as any)?.upgradeOption === 'Rolling') {
      taskInfo.push({
        label: t('delay'),
        value: isLoading
          ? '-'
          : t('delayValue', {
              delay: (auditData?.payload as any)?.sleepAfterMasterRestartMillis / 1000
            }),
        className: classes.rollingRestart
      });
    }
  }

  if (currentTask.type === TaskType.SOFTWARE_UPGRADE) {
    taskInfo.push({
      label: t('startVersion'),
      value: currentTask.details.versionNumbers?.ybPrevSoftwareVersion ?? '-'
    });
    taskInfo.push({
      label: t('targetVersion'),
      value: currentTask.details.versionNumbers?.ybSoftwareVersion ?? '-'
    });
  }

  return (
    <div className={classes.detailsInfo}>
      {taskInfo.map((task, i) => {
        if (task.isHr) return <div className={classes.hr} key={`div${i}`} />;
        return (
          <Fragment key={i}>
            <div className={clsx(classes.info, task.className)} key={i}>
              <Typography variant="subtitle1" className={classes.label}>
                {task.label}
              </Typography>
              <Typography variant="subtitle1" className={classes.value}>
                {task.value}
              </Typography>
            </div>
          </Fragment>
        );
      })}
    </div>
  );
};

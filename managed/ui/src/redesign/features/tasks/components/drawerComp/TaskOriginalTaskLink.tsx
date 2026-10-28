/*
 * Copyright 2026 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';
import { useDispatch, useSelector } from 'react-redux';
import { useQuery } from 'react-query';
import { Typography, makeStyles } from '@material-ui/core';

import { showTaskInDrawer } from '@app/actions/tasks';
import { api, taskQueryKey } from '@app/redesign/helpers/api';
import { formatDatetime, YBTimeFormats } from '@app/redesign/helpers/DateUtils';
import { getOriginalTaskTypeColumnLabel } from '../../TaskUtils';
import { Task, TaskState } from '../../dtos';
import { TaskDrawerCompProps } from './dtos';

import LoadingIcon from '@app/redesign/assets/default-loading-circles.svg';
import LinkV2Icon from '@app/redesign/assets/approved/link-v2.svg';
import ArrowIcon from '@app/redesign/assets/approved/arrow.svg';

const TRANSLATION_KEY_PREFIX = 'taskDetails.originalTask';
const COMPACT_TIMESTAMP_FORMAT = 'MMM D, h:mm A' as YBTimeFormats;

const useStyles = makeStyles((theme) => ({
  root: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    gap: theme.spacing(2),

    boxSizing: 'border-box',
    width: '100%',
    margin: 0,
    padding: theme.spacing(1.5, 2),

    backgroundColor: theme.palette.common.white,
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.shape.borderRadius,
    color: 'inherit',
    cursor: 'pointer',
    fontFamily: 'inherit',
    textAlign: 'left',

    '&:disabled': {
      cursor: 'default'
    }
  },
  content: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(2),

    minWidth: 0
  },
  tag: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(0.5),

    boxSizing: 'border-box',
    height: 24,
    padding: theme.spacing(0, 0.75),

    backgroundColor: theme.palette.common.white,
    border: `1px solid ${theme.palette.grey[300]}`,
    borderRadius: 6,
    color: theme.palette.grey[900],
    flexShrink: 0,
    whiteSpace: 'nowrap'
  },
  tagLabel: {
    fontSize: 11.5,
    lineHeight: '16px'
  },
  tagIcon: {
    width: 16,
    height: 16,
    minWidth: 16,
    minHeight: 16
  },
  metadata: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(0.75),

    minWidth: 0,
    color: theme.palette.common.black,
    fontSize: 13,
    lineHeight: '16px',
    whiteSpace: 'nowrap'
  },
  taskType: {
    fontWeight: 600,
    overflow: 'hidden',
    textOverflow: 'ellipsis'
  },
  separator: {
    color: theme.palette.common.black,
    fontWeight: 600
  },
  timestamp: {
    fontWeight: 400,
    overflow: 'hidden',
    textOverflow: 'ellipsis'
  },
  status: {
    fontWeight: 600
  },
  statusFailure: {
    color: theme.palette.ybacolors.pillDangerIcon
  },
  statusDefault: {
    color: theme.palette.common.black
  },
  loading: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(0.5),

    color: theme.palette.grey[700]
  },
  loadingLabel: {
    fontSize: 13,
    fontWeight: 400,
    lineHeight: '16px'
  },
  loadingIcon: {
    width: 16,
    height: 16,
    minWidth: 16,
    minHeight: 16
  },
  arrow: {
    width: 24,
    height: 24,
    minWidth: 24,
    minHeight: 24,

    color: theme.palette.grey[900],
    flexShrink: 0
  }
}));

const getStatusClassName = (
  status: TaskState,
  classes: ReturnType<typeof useStyles>
): string => {
  switch (status) {
    case TaskState.FAILURE:
    case TaskState.ABORTED:
      return classes.statusFailure;
    default:
      return classes.statusDefault;
  }
};

export const TaskOriginalTaskLink: FC<TaskDrawerCompProps> = ({ currentTask }) => {
  const classes = useStyles();
  const dispatch = useDispatch();
  const customerTaskList = useSelector(
    (state: { tasks: { customerTaskList: Task[] } }) => state.tasks.customerTaskList
  );
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const { t: tInfo } = useTranslation('translation', { keyPrefix: 'taskDetails.info' });

  const originalTaskUUID = currentTask.originalTaskUUID;
  const shouldShowLink =
    !!originalTaskUUID && originalTaskUUID !== currentTask.id;

  const originalTaskQuery = useQuery(
    taskQueryKey.detail(originalTaskUUID ?? ''),
    () => api.fetchTaskStatus(originalTaskUUID!),
    {
      enabled: shouldShowLink
    }
  );

  if (!shouldShowLink) {
    return null;
  }

  const isLoading = originalTaskQuery.isLoading;
  const originalTask = originalTaskQuery.data;
  const originalTaskTypeLabel = originalTaskUUID
    ? getOriginalTaskTypeColumnLabel(
        originalTaskUUID,
        customerTaskList,
        originalTask,
        currentTask
      )
    : undefined;

  const handleClick = () => {
    if (isLoading || !originalTaskUUID) {
      return;
    }
    dispatch(showTaskInDrawer(originalTaskUUID));
  };

  return (
    <button
      type="button"
      className={classes.root}
      onClick={handleClick}
      disabled={isLoading}
      data-testid="task-original-task-link"
    >
      <div className={classes.content}>
        <div className={classes.tag}>
          <LinkV2Icon className={classes.tagIcon} />
          <Typography variant="subtitle1" component="span" color="inherit" className={classes.tagLabel}>
            {t('label')}
          </Typography>
        </div>
        {isLoading ? (
          <div className={classes.loading}>
            <Typography
              variant="body2"
              component="span"
              color="inherit"
              className={classes.loadingLabel}
            >
              {t('loading')}
            </Typography>
            <LoadingIcon className={classes.loadingIcon} />
          </div>
        ) : originalTask && originalTaskTypeLabel ? (
          <div className={classes.metadata}>
            <span className={classes.taskType} title={originalTask.title}>
              {originalTaskTypeLabel}
            </span>
            <span className={classes.separator}>·</span>
            <span className={classes.timestamp}>
              {formatDatetime(originalTask.createTime, COMPACT_TIMESTAMP_FORMAT)}
            </span>
            <span className={classes.separator}>·</span>
            <span className={clsx(classes.status, getStatusClassName(originalTask.status, classes))}>
              {tInfo(originalTask.status)}
            </span>
          </div>
        ) : null}
      </div>
      <ArrowIcon className={classes.arrow} />
    </button>
  );
};

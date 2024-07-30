/*
 * Created on Wed Dec 20 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import clsx from 'clsx';
import { useToggle } from 'react-use';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { Collapse, Typography, makeStyles } from '@material-ui/core';
import { YBButton } from '../../../../components';
import { YBLoadingCircleIcon } from '../../../../../components/common/indicators';
import { getFailedTaskDetails } from './api';
import { TaskDetails, TaskStates } from '../../dtos';
import { TaskDrawerCompProps } from './dtos';
import { isTaskFailed } from '../../TaskUtils';
import LinkIcon from '../../../../assets/link.svg';

const useStyles = makeStyles((theme) => ({
  root: {
    maxHeight: '500px',
    overflowY: 'auto',
    '&>div': {
      marginBottom: '20px'
    }
  },
  failedTask: {
    borderRadius: '8px',
    border: `1px solid  ${theme.palette.grey[200]}`,
    background: '#FEEDED',
    minHeight: '48px',
    padding: '8px 12px',
    wordBreak: 'break-word',
    '&>div': {
      marginBottom: '16px'
    },
    display: 'flex',
    justifyContent: 'space-between',
    gap: '8px'
  },
  expandMoreButton: {
    flex: '0 0 90px'
  },
  showLog: {
    fontSize: '12px',
    fontStyle: 'normal',
    fontWeight: 400,
    lineHeight: 'normal',
    textDecorationLine: 'underline',
    '& > img': {
      marginLeft: '5px'
    },
    textAlign: 'right',
    cursor: 'pointer'
  }
}));

export const SubTaskDetails: FC<TaskDrawerCompProps> = ({ currentTask }) => {
  const classes = useStyles();
  const [expandDetails, toggleExpandDetails] = useToggle(false);
  const failedTask = isTaskFailed(currentTask);

  const { t } = useTranslation('translation', {
    keyPrefix: 'taskDetails.progress'
  });

  const { data: failedSubTasks, isLoading } = useQuery(
    ['failed_task', currentTask.id],
    () => getFailedTaskDetails(currentTask.id!),
    {
      enabled: !!currentTask && failedTask,
      select: (data) => data.data
    }
  );

  if (!currentTask) return null;

  const getFailedTaskData = () => {
    if (isLoading) return <YBLoadingCircleIcon />;
    return (
      <Collapse in={expandDetails} collapsedSize={50}>
        <div className={classes.failedTask}>
          {
            <>
              <div>
                {failedSubTasks?.failedSubTasks.map((task) => (
                  <div>{task.errorString}</div>
                ))}
              </div>
              <YBButton
                variant="secondary"
                className={classes.expandMoreButton}
                onClick={() => toggleExpandDetails(!expandDetails)}
                data-testid="expand-failed-task"
              >
                {t('expand')}
              </YBButton>
            </>
          }
        </div>
      </Collapse>
    );
  };

  return (
    <div className={classes.root}>
      {failedTask && getFailedTaskData()}
      {currentTask.status !== TaskStates.RUNNING && (
        <div
          className={classes.showLog}
          onClick={() => {
            window.open(`/logs/?queryRegex=${currentTask.correlationId}`, '_blank');
          }}
        >
          {t('showLog')}
          <img src={LinkIcon} alt="link" />
        </div>
      )}

      {[...currentTask.details.taskDetails].map((task, index) => (
        <SubTaskCard key={index} subTask={task} index={index + 1} />
      ))}
    </div>
  );
};

export type SubTaskCardProps = {
  subTask: TaskDetails;
  index: number;
};

const subTaskCardStyles = makeStyles((theme) => ({
  card: {
    borderRadius: '8px',
    border: `1px solid  ${theme.palette.grey[200]}`,
    background: theme.palette.common.white,
    minHeight: '48px',
    padding: '8px 12px'
  },
  header: {
    display: 'flex',
    alignItems: 'center',
    gap: '8px',
    cursor: 'pointer',
    userSelect: 'none',
    color: theme.palette.grey[900]
  },
  caret: {
    fontSize: '16px'
  },
  indexCircle: {
    height: '32px',
    width: '32px',
    background: theme.palette.grey[100],
    borderRadius: '50%',
    textAlign: 'center',
    lineHeight: '32px',
    color: theme.palette.grey[600],
    fontSize: '14px',
    fontWeight: 400,
    '&.Running': {
      background: theme.palette.primary[200],
      '& i': {
        color: theme.palette.primary[600]
      }
    },
    '&.Success': {
      background: '#DCF8EC',
      '& i': {
        color: theme.palette.success[500]
      }
    },
    '&.Failure,&.Aborted,&.Abort': {
      background: theme.palette.error[100],
      '& i': {
        color: theme.palette.error[500]
      }
    }
  },
  content: {
    padding: '8px 10px',
    marginLeft: '46px',
    marginBottom: '8px',
    borderRadius: '8px',
    marginTop: '16px',
    background: theme.palette.grey[100],
    '&.Success,&.Created': {
      background: '#EEDED'
    },
    '&.Failure,&.Aborted,&.Abort': {
      background: '#FEEDED'
    }
  },
  rowCollapsed: {
    transitionDuration: '0.2s',
    transform: 'rotate(0deg)'
  },
  rowExpanded: {
    transitionDuration: '0.2s',
    transform: 'rotate(90deg)'
  }
}));

export const SubTaskCard: FC<SubTaskCardProps> = ({ subTask, index }) => {
  const classes = subTaskCardStyles();

  const [showDetails, toggleDetails] = useToggle(false);

  const getTaskIcon = () => {
    switch (subTask.state) {
      case TaskStates.RUNNING:
        return <i className={'fa fa-spinner fa-pulse'} />;
      case TaskStates.SUCCESS:
        return <i className={'fa fa-check'} />;
      case TaskStates.ABORTED:
      case TaskStates.FAILURE:
      case TaskStates.ABORT:
        return <i className={'fa fa-exclamation-circle'} />;
      case TaskStates.CREATED:
      case TaskStates.INITIALIZING:
      case TaskStates.UNKNOWN:
      default:
        return index;
    }
  };

  return (
    <div className={classes.card}>
      <div className={classes.header} onClick={() => toggleDetails(!showDetails)}>
        <i
          className={clsx(
            'fa fa-caret-right',
            showDetails ? classes.rowExpanded : classes.rowCollapsed,
            classes.caret
          )}
        />
        <div className={clsx(classes.indexCircle, subTask.state)}>
          {/* {isTaskRunning ? <i className={clsx("fa fa-spinner fa-pulse", classes.spinner)} /> : index} */}
          {getTaskIcon()}
        </div>
        <Typography variant="body2">{subTask.title}</Typography>
      </div>
      <Collapse in={showDetails}>
        <div className={clsx(classes.content, subTask.state)}>
          <Typography variant="subtitle1">{subTask.description}</Typography>
        </div>
      </Collapse>
    </div>
  );
};

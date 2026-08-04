/*
 * Created on Wed Dec 20 2023
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useEffect } from 'react';
import clsx from 'clsx';
import { useMap, useMount, usePrevious, useToggle } from 'react-use';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { keys, sortBy, startCase, values } from 'lodash';
import { Collapse, Tooltip, Typography, makeStyles } from '@material-ui/core';
import { YBButton } from '../../../../components';
import { YBLoadingCircleIcon } from '../../../../../components/common/indicators';
import { getFailedTaskDetails, getSubTaskDetails } from './api';
import { SubTaskInfo, Task, TaskState } from '../../dtos';
import { TaskDrawerCompProps } from './dtos';
import { isTaskFailed, isTaskRunning } from '../../TaskUtils';
import LinkIcon from '../../../../assets/link.svg?img';

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
    flex: '0 0 100px'
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
  const [expandedSubTasks, { setAll, set, get }] = useMap();

  const failedTask = isTaskFailed(currentTask);
  const currentTaskPrevState = usePrevious(currentTask);
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

  const {
    data: detailedTaskInfo,
    isLoading: isSubTaskLoading,
    refetch: refetchSubTasks
  } = useQuery(['subTasks', currentTask.id!], () => getSubTaskDetails(currentTask.id!), {
    select: (data) => data.data,
    enabled: !!currentTask,
    refetchInterval: (data) => {
      return values(data?.[currentTask.targetUUID]).some((task) => isTaskRunning(task))
        ? 8000
        : false;
    }
  });

  useEffect(() => {
    // when the current task is success, we stop refetching the subtasks(see above function)
    // so we need to refetch the subtasks when the task is success
    // we keep track of the previous state of the task to compare the percent complete
    if (currentTaskPrevState?.percentComplete !== currentTask.percentComplete) {
      refetchSubTasks();
    }
  }, [currentTask, currentTaskPrevState]);

  if (!currentTask) return null;

  const subTasksList: Array<{
    key: string;
    subTasks: SubTaskInfo[];
  }> = [];

  // sort the tasks by position
  // if two consecutive tasks have the same subtask group type, group them together
  const sortedSubTasks = sortBy(
    values(detailedTaskInfo?.[currentTask.targetUUID]?.[0].subtaskInfos),
    'position'
  );
  let subTasksListIndex = 0;
  let sortedSubTaskIndex = 0;
  // loop through the sorted subtasks
  while (sortedSubTaskIndex < sortedSubTasks.length) {
    const subTask = sortedSubTasks[sortedSubTaskIndex];
    const subTaskGroup = subTask.subTaskGroupType;

    // if the subtask group type is different from the previous one, create a new group
    if (subTasksList[subTasksListIndex - 1]?.key !== subTaskGroup) {
      subTasksList.push({
        key: subTaskGroup,
        subTasks: []
      });
      subTasksListIndex++;
    }
    // if the subtask group type is the same as the previous one, push the subtask to the previous group
    if (subTasksList[subTasksListIndex - 1].key === subTaskGroup) {
      subTasksList[subTasksListIndex - 1].subTasks.push(subTask);
    }

    sortedSubTaskIndex++;
  }

  const getFailedTaskData = () => {
    if (isLoading) return <YBLoadingCircleIcon />;
    return (
      <Collapse in={expandDetails} collapsedSize={50}>
        <div className={classes.failedTask}>
          {
            <>
              <div>
                {failedSubTasks?.failedSubTasks.map((task, i) => (
                  <div key={i}>{task.errorString}</div>
                ))}
              </div>
              <YBButton
                variant="secondary"
                className={classes.expandMoreButton}
                onClick={() => toggleExpandDetails(!expandDetails)}
                data-testid="expand-failed-task"
              >
                {t(expandDetails ? 'viewLess' : 'expand')}
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
      <div
        className={classes.showLog}
        onClick={() => {
          window.open(
            `/logs/?queryRegex=${currentTask.correlationId}&startDate=${currentTask.createTime}`,
            '_blank'
          );
        }}
        data-testid="show-log"
      >
        {t('showLog')}
        <img src={LinkIcon} alt="link" />
      </div>

      {isSubTaskLoading ? (
        <YBLoadingCircleIcon />
      ) : (
        <>
          {subTasksList.length > 0 && (
            <div
              className={classes.showLog}
              onClick={() => {
                const allExpanded = keys(expandedSubTasks).every((key) => expandedSubTasks[key]);
                setAll(keys(expandedSubTasks).map(() => !allExpanded));
              }}
              data-testid="expand-all-subtasks"
            >
              {t(
                keys(expandedSubTasks).every((k) => expandedSubTasks[k])
                  ? 'collapseAll'
                  : 'expandAll'
              )}
            </div>
          )}
          {subTasksList.map((subTask, index) => (
            <SubTaskCard
              key={index}
              index={index}
              category={subTask.key}
              subTasks={subTask.subTasks}
              expanded={get(index) ?? false}
              toggleExpanded={(index) => {
                set(index, get(index) === undefined ? false : !get(index));
              }}
            />
          ))}
        </>
      )}
    </div>
  );
};

export type SubTaskCardProps = {
  subTasks: SubTaskInfo[];
  index: number;
  category: string;
  expanded: boolean;
  toggleExpanded: (index: number) => void;
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
  subTaskPanel: {
    background: 'rgba(240, 244, 247, 0.50)',
    display: 'flex',
    flexDirection: 'column',
    gap: '24px',
    padding: '14px',
    marginLeft: '20px',
    marginTop: '16px',
    borderRadius: '8px'
  },
  content: {
    borderRadius: '50%',
    display: 'flex',
    alignItems: 'center',
    flexFlow: 'wrap',
    gap: '20px',
    '&.Success,&.Created': {
      '& i': {
        color: theme.palette.success[500]
      }
    },
    '&.Failure,&.Aborted,&.Abort': {
      '& i': {
        color: theme.palette.error[500]
      }
    }
  },
  rowCollapsed: {
    transitionDuration: '0.2s',
    transform: 'rotate(0deg)'
  },
  rowExpanded: {
    transitionDuration: '0.2s',
    transform: 'rotate(90deg)'
  },
  errMsg: {
    width: '100%',
    marginLeft: '50px',
    borderRadius: '8px',
    background: theme.palette.error[100],
    padding: '8px 10px',
    wordBreak: 'break-word'
  },
  timeElapsed: {
    marginLeft: 'auto'
  }
}));

export const SubTaskCard: FC<SubTaskCardProps> = ({
  subTasks,
  index,
  category,
  expanded,
  toggleExpanded
}) => {
  const classes = subTaskCardStyles();
  useMount(() => {
    toggleExpanded(index);
  });

  const getTaskIcon = (state: Task['status'], position?: number) => {
    switch (state) {
      case TaskState.RUNNING:
        return <i className={'fa fa-spinner fa-pulse'} />;
      case TaskState.SUCCESS:
        return <i className={'fa fa-check'} />;
      case TaskState.ABORTED:
      case TaskState.FAILURE:
      case TaskState.ABORT:
        return <i className={'fa fa-exclamation-circle'} />;
      case TaskState.CREATED:
      case TaskState.INITIALIZING:
      case TaskState.UNKNOWN:
      default:
        return position ?? index;
    }
  };

  // Get the category status based on the subtasks
  // If any of the subtasks is not success, then the category status is the status of the last subtask
  // If all the subtasks are success, then the category status is success
  let categoryTaskStatus = TaskState.CREATED;

  for (let i = 0; i < subTasks.length; i++) {
    if (subTasks[i].taskState !== TaskState.SUCCESS) {
      categoryTaskStatus = subTasks[i].taskState;
      break;
    }
    categoryTaskStatus = TaskState.SUCCESS;
  }

  const getNodeNames = (subTask: SubTaskInfo) => {
    if (subTask.taskParams?.nodeDetailsSet) {
      return (
        <>
          {subTask.taskParams.nodeDetailsSet.map((node) => (
            <div>{`(${node.nodeName})`}</div>
          ))}
        </>
      );
    }
    if (subTask.taskParams?.nodeName) {
      return ` (${subTask.taskParams.nodeName})`;
    }
    return '';
  };

  return (
    <div className={classes.card} key={index}>
      <div className={classes.header} onClick={() => toggleExpanded(index)}>
        <i
          className={clsx(
            'fa fa-caret-right',
            expanded ? classes.rowExpanded : classes.rowCollapsed,
            classes.caret
          )}
        />
        <div className={clsx(classes.indexCircle, categoryTaskStatus)}>
          {getTaskIcon(categoryTaskStatus, index + 1)}
        </div>
        <Typography variant="body2">{startCase(category)}</Typography>
      </div>
      <Collapse in={expanded}>
        <div className={classes.subTaskPanel}>
          {subTasks.map((subTask, index) => {
            return (
              <div className={clsx(classes.content, subTask.taskState)} key={index}>
                <div className={clsx(classes.indexCircle, subTask.taskState)}>
                  {getTaskIcon(subTask.taskState, index + 1)}
                </div>
                <Tooltip title={getNodeNames(subTask)} placement="top" arrow>
                  <Typography variant="body2">{startCase(subTask.taskType)}</Typography>
                </Tooltip>
                {subTask.details?.error?.originMessage && (
                  <div className={classes.errMsg}>{subTask.details?.error?.originMessage}</div>
                )}
              </div>
            );
          })}
        </div>
      </Collapse>
    </div>
  );
};

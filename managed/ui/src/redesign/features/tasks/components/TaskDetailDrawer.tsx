/*
 * Created on Wed Dec 20 2023
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useEffect, useState } from 'react';
import { useSessionStorage } from 'react-use';
import { useDispatch, useSelector } from 'react-redux';
import { find } from 'lodash';
import { useTranslation } from 'react-i18next';
import { Snackbar, makeStyles } from '@material-ui/core';
import { Alert } from '@material-ui/lab';

import { YBSidePanel } from '../../../components';
import { TaskDetailActions } from './drawerComp/TaskDetailActions';
import { TaskDetailsHeader } from './drawerComp/TaskDetailHeader';
import { TaskDetailInfo } from './drawerComp/TaskDetailInfo';
import { TaskDetailProgress } from './drawerComp/TaskDetailProgress';
import { SubTaskDetails } from './drawerComp/SubTaskDetails';
import { YBLoadingCircleIcon } from '../../../../components/common/indicators';

import {
  fetchCustomerTasks,
  fetchCustomerTasksFailure,
  fetchCustomerTasksSuccess,
  hideTaskInDrawer
} from '../../../../actions/tasks';

import { Task } from '../dtos';

const useStyles = makeStyles((theme) => ({
  root: {
    width: '600px',
    background: theme.palette.ybacolors.backgroundGrayLightest
  },
  dialogContent: {
    padding: '0 !important'
  },
  content: {
    padding: '0px 20px',
    '& > *': {
      marginTop: '16px'
    }
  }
}));

enum taskRetryStates {
  RETRIED_LOADING,
  RETRIED_FINISHED
}

export const TaskDetailDrawer: FC = () => {
  const classes = useStyles();
  const dispatch = useDispatch();

  const [currentTask, setCurrentTask] = useState<Task | null>(null);
  const taskUUID = useSelector((data: any) => data.tasks.showTaskInDrawer);
  const visible = taskUUID !== '';

  const [taskRetries, setTaskRetryStatus] = useSessionStorage<Record<string, taskRetryStates>>(
    `task_retries`,
    {}
  );

  const { t } = useTranslation('translation', {
    keyPrefix: 'taskDetails'
  });

  const taskList = useSelector((data: any) => data.tasks);

  const refetchTask = () => {
    dispatch(fetchCustomerTasks() as any).then((response: any) => {
      if (!response.error) {
        setTaskRetryStatus({
          ...taskRetries,
          [taskUUID]: taskRetryStates.RETRIED_FINISHED
        });
        dispatch(fetchCustomerTasksSuccess(response.payload));
      } else {
        dispatch(fetchCustomerTasksFailure(response.payload));
      }
    });
  };

  useEffect(() => {
    const task = find(taskList.customerTaskList, { id: taskUUID });
    const taskRetryStatus = taskRetries[taskUUID];
    if (task) {
      setCurrentTask(task);
    } else if (taskRetryStatus === undefined && taskUUID !== null) {
      setTaskRetryStatus({
        ...taskRetries,
        [taskUUID]: taskRetryStates.RETRIED_LOADING
      });
      // if we don't find the task in the store (created just now), refresh the task list in redux store
      refetchTask();
    }
    return () => {
      setCurrentTask(null);
    };
  }, [taskUUID, taskList, taskRetries]);

  const onHide = () => {
    dispatch(hideTaskInDrawer());
  };

  if (visible && !currentTask && taskRetries[taskUUID] === taskRetryStates.RETRIED_LOADING) {
    return <YBLoadingCircleIcon size="small" />;
  }
  // we did try refetching the tasks , but still can't find the task.(i.e task might be deleted).
  // we show an error toast.
  // but, how are we supposed to find the deleted task?. we can find those attached to the old backups.
  // task uuid is present, but the original task is deleted.
  if (!currentTask && taskRetries[taskUUID] === taskRetryStates.RETRIED_FINISHED && visible) {
    return (
      <Snackbar
        open={visible}
        autoHideDuration={6000}
        onClose={onHide}
        anchorOrigin={{
          vertical: 'top',
          horizontal: 'center'
        }}
      >
        <Alert onClose={onHide} severity="error">
          {t('taskNotfound')}
        </Alert>
      </Snackbar>
    );
  }

  if (!currentTask) return null;

  return (
    <YBSidePanel
      open={visible}
      onClose={onHide}
      overrideWidth="600px"
      dialogContentProps={{
        className: classes.dialogContent
      }}
      enableBackdropDismiss
      keepMounted
    >
      <TaskDetailsHeader onClose={onHide} />
      <div className={classes.content}>
        <TaskDetailActions currentTask={currentTask} />
        <TaskDetailInfo currentTask={currentTask} />
        <TaskDetailProgress currentTask={currentTask} />
        <SubTaskDetails currentTask={currentTask} />
      </div>
    </YBSidePanel>
  );
};

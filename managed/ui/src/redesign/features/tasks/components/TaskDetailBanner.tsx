/*
 * Created on Thu Dec 21 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useCallback, useEffect } from 'react';
import { useSessionStorage, useUnmount } from 'react-use';
import { useSelector } from 'react-redux';
import { find, noop } from 'lodash';
import { TaskDetailDrawer } from './TaskDetailDrawer';
import { TaskInProgressBanner } from './bannerComp/TaskInProgressBanner';
import { TaskSuccessBanner } from './bannerComp/TaskSuccessBanner';
import { TaskFailedBanner } from './bannerComp/TaskFailedBanner';
import { Task, TaskStates } from '../dtos';
import { useIsTaskNewUIEnabled } from '../TaskUtils';

type TaskDetailBannerProps = {
  taskUUID: string;
  universeUUID?: string;
};

export const TaskDetailBanner: FC<TaskDetailBannerProps> = ({ taskUUID, universeUUID }) => {
  //We use session storage to prevent the states getting reset to defaults incase of re-rendering.
  const [taskID, setTaskID] = useSessionStorage<string | null>(`taskID`, taskUUID);

  const [showTaskDetailsDrawer, toggleTaskDetailsDrawer] = useSessionStorage(
    `show-task-detail`,
    false
  );

  // instead of using react query , we use the data from the redux store.
  // Old task components use redux store. We want to make sure we display the same progress across the ui.
  const taskList = useSelector((data: any) => data.tasks);

  const task: Task | undefined = find(taskList.customerTaskList, { id: taskID });

  useEffect(() => {
    if (taskUUID) {
      setTaskID(taskUUID);
    }
    // if we move away from universe, close the banner
    if (!universeUUID) {
      setTaskID(null);
    }
  }, [taskUUID, universeUUID]);

  useUnmount(() => {
    setTaskID(null);
  });

  const DrawerComp = (
    <TaskDetailDrawer
      visible={showTaskDetailsDrawer}
      onClose={() => {
        toggleTaskDetailsDrawer(false);
      }}
      taskUUID={taskID!}
    />
  );

  // display banner based on type
  const bannerComp = useCallback((task: Task) => {
    switch (task.status) {
      case TaskStates.RUNNING:
        return (
          <TaskInProgressBanner
            currentTask={task}
            viewDetails={() => {
              toggleTaskDetailsDrawer(true);
            }}
            onClose={noop}
          />
        );
      case TaskStates.SUCCESS:
        return (
          <TaskSuccessBanner
            currentTask={task}
            viewDetails={() => {
              toggleTaskDetailsDrawer(true);
            }}
            onClose={() => setTaskID(null)}
          />
        );
      case TaskStates.FAILURE:
        return (
          <TaskFailedBanner
            currentTask={task}
            viewDetails={() => {
              toggleTaskDetailsDrawer(true);
            }}
            onClose={() => setTaskID(null)}
          />
        );
      default:
        return null;
    }
  }, []);

  const isNewTaskDetailsUIEnabled = useIsTaskNewUIEnabled();
  if (!isNewTaskDetailsUIEnabled) return null;

  if (universeUUID && task?.targetUUID !== universeUUID) return null;

  if (!task && showTaskDetailsDrawer) {
    return DrawerComp;
  }

  if (!task) return null;

  return (
    <>
      {bannerComp(task)}
      {DrawerComp}
    </>
  );
};

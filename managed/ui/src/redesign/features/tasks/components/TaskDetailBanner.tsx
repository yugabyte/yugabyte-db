/*
 * Created on Thu Dec 21 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useCallback } from 'react';
import { useDispatch, useSelector } from 'react-redux';
import { find, noop, values } from 'lodash';
import { TaskInProgressBanner } from './bannerComp/TaskInProgressBanner';
import { TaskSuccessBanner } from './bannerComp/TaskSuccessBanner';
import { TaskFailedBanner } from './bannerComp/TaskFailedBanner';
import { TaskFailedSoftwareUpgradeBanner } from './bannerComp/TaskFailedSoftwareUpgradeBanner';
import { isSoftwareUpgradeFailed, useIsTaskNewUIEnabled } from '../TaskUtils';
import { hideTaskBanner, hideTaskInDrawer, showTaskInDrawer } from '../../../../actions/tasks';
import { Task, TaskStates } from '../dtos';

type TaskDetailBannerProps = {
  taskUUID: string;
  universeUUID?: string;
};

export const TaskDetailBanner: FC<TaskDetailBannerProps> = ({ universeUUID }) => {
  //We use session storage to prevent the states getting reset to defaults incase of re-rendering.
  const dispatch = useDispatch();

  const bannerTaskInfo = useSelector((data: any) => data.tasks.taskBannerInfo);
  const universeData = useSelector((data: any) => data.universe?.currentUniverse?.data);

  const tasksInUniverse = bannerTaskInfo[universeUUID!];

  const taskUUID = values(tasksInUniverse)
    .filter((t) => t.visible)
    .sort((a, b) => b.timestamp - a.timestamp)?.[0]?.taskUUID;

  const toggleTaskDetailsDrawer = (flag: boolean) => {
    if (flag) {
      dispatch(showTaskInDrawer(taskUUID));
    } else {
      dispatch(hideTaskInDrawer());
    }
  };

  // instead of using react query , we use the data from the redux store.
  // Old task components use redux store. We want to make sure we display the same progress across the ui.
  const taskList = useSelector((data: any) => data.tasks);

  const task: Task | undefined = find(taskList.customerTaskList, { id: taskUUID });

  const hideBanner = () => {
    dispatch(hideTaskBanner(taskUUID, universeUUID));
  };

  // display banner based on type
  const bannerComp = useCallback(
    (task: Task) => {
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
              onClose={() => hideBanner()}
            />
          );
        case TaskStates.FAILURE:
          if (isSoftwareUpgradeFailed(task, universeData)) {
            return (
              <TaskFailedSoftwareUpgradeBanner
                currentTask={task}
                viewDetails={() => {
                  toggleTaskDetailsDrawer(true);
                }}
                onClose={() => hideBanner()}
              />
            );
          }
          return (
            <TaskFailedBanner
              currentTask={task}
              viewDetails={() => {
                toggleTaskDetailsDrawer(true);
              }}
              onClose={() => hideBanner()}
            />
          );
        default:
          return null;
      }
    },
    [taskUUID]
  );

  const isNewTaskDetailsUIEnabled = useIsTaskNewUIEnabled();
  if (!isNewTaskDetailsUIEnabled) return null;

  if (universeUUID && task?.targetUUID !== universeUUID) return null;

  if (!task) return null;
  return <>{bannerComp(task)}</>;
};

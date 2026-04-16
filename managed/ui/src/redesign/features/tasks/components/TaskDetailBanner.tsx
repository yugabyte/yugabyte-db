/*
 * Created on Thu Dec 21 2023
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useCallback } from 'react';
import moment from 'moment';
import { useDispatch, useSelector } from 'react-redux';
import { useLocalStorage } from 'react-use';
import { noop, values } from 'lodash';
import { TaskInProgressBanner } from './bannerComp/TaskInProgressBanner';
import { TaskSuccessBanner } from './bannerComp/TaskSuccessBanner';
import { TaskFailedBanner } from './bannerComp/TaskFailedBanner';
import { TaskFailedSoftwareUpgradeBanner } from './bannerComp/TaskFailedSoftwareUpgradeBanner';
import { isSoftwareUpgradeFailed, useIsTaskNewUIEnabled } from '../TaskUtils';
import { hideTaskInDrawer, showTaskInDrawer } from '../../../../actions/tasks';
import { Task, TaskState } from '../dtos';

type TaskDetailBannerProps = {
  universeUUID: string;
};

export const TaskDetailBanner: FC<TaskDetailBannerProps> = ({ universeUUID }) => {
  //We use session storage to prevent the states getting reset to defaults incase of re-rendering.
  const dispatch = useDispatch();

  const universeData = useSelector((data: any) => data.universe?.currentUniverse?.data);

  // we use localStorage to hide the banner for the task, if it is already closed.
  const [acknowlegedTasks, setAcknowlegedTasks] = useLocalStorage<Record<string, string>>(
    'acknowlegedTasks',
    {}
  );

  // instead of using react query , we use the data from the redux store.
  // Old task components use redux store. We want to make sure we display the same progress across the ui.
  const taskList = useSelector((data: any) => data.tasks);

  const tasksInUniverse = taskList.customerTaskList;

  // always display the last task in the banner
  const task = values(tasksInUniverse)
    .filter((t) => t.targetUUID === universeUUID)
    .sort((a, b) => (moment(b.createTime).isBefore(a.createTime) ? -1 : 1))[0];

  const taskUUID = task?.id;

  const toggleTaskDetailsDrawer = (flag: boolean) => {
    if (flag) {
      dispatch(showTaskInDrawer(taskUUID));
    } else {
      dispatch(hideTaskInDrawer());
    }
  };

  const hideBanner = () => {
    setAcknowlegedTasks({ ...acknowlegedTasks, [universeUUID!]: taskUUID });
  };

  // display banner based on type
  const bannerComp = useCallback(
    (task: Task) => {
      switch (task.status) {
        case TaskState.RUNNING:
          return (
            <TaskInProgressBanner
              currentTask={task}
              viewDetails={() => {
                toggleTaskDetailsDrawer(true);
              }}
              onClose={noop}
            />
          );
        case TaskState.SUCCESS:
          return (
            <TaskSuccessBanner
              currentTask={task}
              viewDetails={() => {
                toggleTaskDetailsDrawer(true);
              }}
              onClose={() => hideBanner()}
            />
          );
        case TaskState.FAILURE:
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
  if (universeUUID && acknowlegedTasks?.[universeUUID] === taskUUID) return null;

  if (!isNewTaskDetailsUIEnabled) return null;

  if (universeUUID && task?.targetUUID !== universeUUID) return null;

  if (!task) return null;
  return <>{bannerComp(task)}</>;
};

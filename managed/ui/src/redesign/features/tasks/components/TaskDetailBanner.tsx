/*
 * Created on Thu Dec 21 2023
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useCallback, useEffect, useRef, useState } from 'react';
import moment from 'moment';
import { useDispatch, useSelector } from 'react-redux';
import { useQuery, useQueryClient } from 'react-query';
import { useLocalStorage } from 'react-use';
import { noop, values } from 'lodash';
import { makeStyles } from '@material-ui/core';
import { useTranslation } from 'react-i18next';

import { TASK_SHORT_TIMEOUT } from '@app/components/tasks/constants';
import { DbUpgradeManagementSidePanel } from '@app/redesign/features/universe/universe-actions/software-upgrade/upgrade-management/DbUpgradeManagementSidePanel';
import {
  api,
  runtimeConfigQueryKey,
  taskQueryKey,
  universeQueryKey
} from '@app/redesign/helpers/api';
import { RuntimeConfigKey } from '@app/redesign/helpers/constants';
import { getGetUniverseQueryKey, getUniverse } from '@app/v2/api/universe/universe';
import { TaskInProgressBanner } from './bannerComp/TaskInProgressBanner';
import { TaskSuccessBanner } from './bannerComp/TaskSuccessBanner';
import { TaskFailedBanner } from './bannerComp/TaskFailedBanner';
import { TaskFailedSoftwareUpgradeBanner } from './bannerComp/TaskFailedSoftwareUpgradeBanner';
import {
  getIsDbUpgradeFinalizeTask,
  getIsDbUpgradePrecheckTask,
  getIsDbUpgradeRollbackTask,
  getIsDbUpgradeTask,
  isSoftwareUpgradeFailed,
  useIsTaskNewUIEnabled
} from '../TaskUtils';
import {
  hideTaskInDrawer,
  patchTasksForCustomer,
  showTaskInDrawer
} from '../../../../actions/tasks';
import { Task, TaskState } from '../dtos';
import { DbUpgradeFinalizeTaskBanner } from './clusterBanner/DbUpgradeFinalizeTaskBanner';
import { DbUpgradePrecheckTaskBanner } from './clusterBanner/DbUpgradePrecheckTaskBanner';
import { DbUpgradeRollbackTaskBanner } from './clusterBanner/DbUpgradeRollbackTaskBanner';
import { DbUpgradeTaskBanner } from './clusterBanner/DbUpgradeTaskBanner';
import {
  ClusterOperationBanner,
  ClusterOperationBannerType
} from './clusterBanner/ClusterOperationBanner';
import { YBButton } from '@app/redesign/components';
import {
  getUniverseStatus,
  SoftwareUpgradeState,
  UniverseState
} from '@app/components/universes/helpers/universeHelpers';
import { PollingIntervalMs } from '@app/components/xcluster/constants';

const useStyles = makeStyles((theme) => ({
  bannerContainer: {
    padding: theme.spacing(1, 2.5),
    backgroundColor: theme.palette.common.white
  },
  bannersContainer: {
    display: 'flex',
    flexDirection: 'column'
  }
}));

type TaskDetailBannerProps = {
  universeUUID: string;
};

export const TaskDetailBanner: FC<TaskDetailBannerProps> = ({ universeUUID }) => {
  const [isDbUpgradeManagementSidePanelOpen, setIsDbUpgradeManagementSidePanelOpen] =
    useState(false);
  const dispatch = useDispatch();
  const classes = useStyles();
  const universeData = useSelector((data: any) => data.universe?.currentUniverse?.data);
  const { t } = useTranslation('translation');

  // we use localStorage to hide the banner for the task, if it is already closed.
  const [acknowlegedTasks, setAcknowlegedTasks] = useLocalStorage<Record<string, string>>(
    'acknowlegedTasks',
    {}
  );

  const universeRuntimeConfigsQuery = useQuery(
    runtimeConfigQueryKey.universeScope(universeUUID),
    () => api.fetchRuntimeConfigs(universeUUID),
    { enabled: !!universeUUID }
  );

  const isCanaryUpgradeEnabled =
    universeRuntimeConfigsQuery.data?.configEntries?.find(
      (c: { key: string; value: string }) => c.key === RuntimeConfigKey.ENABLE_CANARY_UPGRADE
    )?.value === 'true';

  const isNewTaskDetailsUIEnabled = useIsTaskNewUIEnabled();

  // This query is used to update the redux store with the latest task list.
  const universeTasksQuery = useQuery(
    taskQueryKey.universe(universeUUID),
    () => api.fetchCustomerTasks(universeUUID),
    {
      enabled: !!universeUUID && isNewTaskDetailsUIEnabled && isCanaryUpgradeEnabled,
      refetchInterval: TASK_SHORT_TIMEOUT,
      onSuccess(data) {
        dispatch(patchTasksForCustomer(universeUUID, data));
      }
    }
  );

  const universeDetailsQuery = useQuery(
    universeQueryKey.detailsV2(universeUUID),
    () => getUniverse(universeUUID),
    {
      enabled: !!universeUUID && isNewTaskDetailsUIEnabled && isCanaryUpgradeEnabled,
      staleTime: PollingIntervalMs.UNIVERSE_STATE
    }
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

  const queryClient = useQueryClient();
  const lastInvalidatedForTaskIdRef = useRef<string | undefined>(undefined);

  // Refetch v2 universe (useGetUniverse) when the banner's newest task for this universe reaches a
  // terminal state. Semantics: follows the latest customerTaskList row for universeUUID, not a specific edit.
  useEffect(() => {
    if (!isNewTaskDetailsUIEnabled || !universeUUID || !task?.id) return;

    const isTerminal =
      task.status === TaskState.SUCCESS ||
      task.status === TaskState.FAILURE ||
      task.status === TaskState.ABORTED;

    if (!isTerminal) return;
    if (lastInvalidatedForTaskIdRef.current === task.id) return;

    lastInvalidatedForTaskIdRef.current = task.id;
    void queryClient.invalidateQueries(getGetUniverseQueryKey(universeUUID));
    void queryClient.invalidateQueries(universeQueryKey.detailsV2(universeUUID));
  }, [isNewTaskDetailsUIEnabled, universeUUID, task?.id, task?.status, queryClient]);

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

  if (!isNewTaskDetailsUIEnabled) return null;

  if (universeUUID && task?.targetUUID !== universeUUID) return null;

  if (!task) return null;

  if (universeRuntimeConfigsQuery.isLoading) {
    return null;
  }

  if (isCanaryUpgradeEnabled) {
    if (getIsDbUpgradePrecheckTask(task)) {
      return (
        <div className={classes.bannerContainer}>
          <DbUpgradePrecheckTaskBanner
            task={task}
            universeUuid={universeUUID}
            onDismiss={hideBanner}
          />
        </div>
      );
    }

    if (getIsDbUpgradeRollbackTask(task)) {
      return (
        <div className={classes.bannerContainer}>
          <DbUpgradeRollbackTaskBanner task={task} universeUuid={universeUUID} />
        </div>
      );
    }

    if (getIsDbUpgradeFinalizeTask(task)) {
      return (
        <div className={classes.bannerContainer}>
          <DbUpgradeFinalizeTaskBanner task={task} universeUuid={universeUUID} />
        </div>
      );
    }

    if (getIsDbUpgradeTask(task)) {
      return (
        <div className={classes.bannerContainer}>
          <DbUpgradeTaskBanner task={task} universeUuid={universeUUID} />
        </div>
      );
    }

    if (universeData?.universeDetails?.softwareUpgradeState === SoftwareUpgradeState.PRE_FINALIZE) {
      const v2UniverseInfo = universeDetailsQuery.data?.info;
      const universeStatus = getUniverseStatus(
        v2UniverseInfo
          ? {
              universeDetails: {
                updateInProgress: v2UniverseInfo.update_in_progress,
                updateSucceeded: v2UniverseInfo.update_succeeded,
                universePaused: v2UniverseInfo.universe_paused,
                placementModificationTaskUuid: v2UniverseInfo.placement_modification_task_uuid,
                errorString: ''
              }
            }
          : undefined
      );
      return (
        <div className={classes.bannersContainer}>
          {universeUUID && acknowlegedTasks?.[universeUUID] === taskUUID ? null : bannerComp(task)}
          {universeStatus.state === UniverseState.GOOD && (
            <>
              <div className={classes.bannerContainer}>
                <ClusterOperationBanner
                  type={ClusterOperationBannerType.PENDING_ACTION_YELLOW}
                  title={t('universeActions.dbUpgrade.clusterBanner.finalizeOrRollBack.title')}
                  actions={
                    <YBButton
                      variant="secondary"
                      size="medium"
                      data-testid="open-upgrade-monitor-button"
                      onClick={() => {
                        setIsDbUpgradeManagementSidePanelOpen(true);
                      }}
                    >
                      {t('universeActions.dbUpgrade.clusterBanner.actions.openUpgradeMonitor')}
                    </YBButton>
                  }
                  description={t(
                    'universeActions.dbUpgrade.clusterBanner.finalizeOrRollBack.description'
                  )}
                />
              </div>
              {isDbUpgradeManagementSidePanelOpen && (
                <DbUpgradeManagementSidePanel
                  modalProps={{
                    open: isDbUpgradeManagementSidePanelOpen,
                    onClose: () => setIsDbUpgradeManagementSidePanelOpen(false)
                  }}
                  universeUuid={universeUUID}
                />
              )}
            </>
          )}
        </div>
      );
    }
  }

  if (universeUUID && acknowlegedTasks?.[universeUUID] === taskUUID) {
    return null;
  }

  return <>{bannerComp(task)}</>;
};

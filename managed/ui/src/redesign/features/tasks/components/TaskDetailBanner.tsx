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
import { OperationBannerVariant, YBOperationBanner } from '@yugabyte-ui-library/core';

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
  getIsEditUniverseRollbackTask,
  getIsEditUniverseTask,
  getIsSoftwareUpgradeLockingTask,
  getLatestSoftwareUpgradeLockingTaskForUniverse,
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
import { EditUniverseRollbackTaskBanner } from './clusterBanner/EditUniverseRollbackTaskBanner';
import { EditUniverseTaskBanner } from './clusterBanner/EditUniverseTaskBanner';
import { OperationBannerWaveIcon } from './clusterBanner/operationBannerIcons';
import { YBButton } from '@app/redesign/components';
import {
  getUniverseStatus,
  SoftwareUpgradeState,
  UniverseState
} from '@app/components/universes/helpers/universeHelpers';
import { PollingIntervalMs } from '@app/components/xcluster/constants';
import { fetchUniverseInfoResponse } from '@app/actions/universe';

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
  useQuery(taskQueryKey.universe(universeUUID), () => api.fetchCustomerTasks(universeUUID), {
    enabled: !!universeUUID && isNewTaskDetailsUIEnabled && isCanaryUpgradeEnabled,
    refetchInterval: TASK_SHORT_TIMEOUT,
    onSuccess(data) {
      dispatch(patchTasksForCustomer(universeUUID, data));
    }
  });

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

  const customerTaskList: Task[] = taskList.customerTaskList ?? [];

  // Primary slot: most recent task for this universe (may stack with software upgrade banner below).
  const lastCreatedTask = values(customerTaskList)
    .filter((t) => t.targetUUID === universeUUID)
    .sort((a, b) => (moment(b.createTime).isBefore(a.createTime) ? -1 : 1))[0];

  const taskUUID = lastCreatedTask?.id;

  const queryClient = useQueryClient();
  const lastInvalidatedForTaskIdRef = useRef<string | undefined>(undefined);

  const refreshUniverse = useCallback(() => {
    return api.fetchUniverse(universeUUID).then((data) => {
      dispatch(fetchUniverseInfoResponse({ data, status: 200 }));
    });
  }, [dispatch, universeUUID]);

  // Refetch v2 universe (useGetUniverse) when the banner's newest task for this universe reaches a
  // terminal state. Semantics: follows the latest customerTaskList row for universeUUID, not a specific edit.
  useEffect(() => {
    if (!isNewTaskDetailsUIEnabled || !universeUUID || !lastCreatedTask?.id) return;

    const isTerminal =
      lastCreatedTask.status === TaskState.SUCCESS ||
      lastCreatedTask.status === TaskState.FAILURE ||
      lastCreatedTask.status === TaskState.ABORTED;

    if (!isTerminal) return;
    if (lastInvalidatedForTaskIdRef.current === lastCreatedTask.id) return;

    lastInvalidatedForTaskIdRef.current = lastCreatedTask.id;
    void refreshUniverse();
    void queryClient.invalidateQueries(getGetUniverseQueryKey(universeUUID));
    void queryClient.invalidateQueries(universeQueryKey.detailsV2(universeUUID));
  }, [
    isNewTaskDetailsUIEnabled,
    universeUUID,
    lastCreatedTask?.id,
    lastCreatedTask?.status,
    queryClient,
    refreshUniverse
  ]);

  const toggleTaskDetailsDrawer = (flag: boolean, drawerTaskUUID?: string) => {
    const drawerTaskId = drawerTaskUUID ?? taskUUID;
    if (flag) {
      dispatch(showTaskInDrawer(drawerTaskId));
    } else {
      dispatch(hideTaskInDrawer());
    }
  };

  const hideBanner = (dismissedTaskId: string) => {
    setAcknowlegedTasks({ ...acknowlegedTasks, [universeUUID!]: dismissedTaskId });
  };

  const isBannerDismissedForTask = (bannerTaskId: string) =>
    !!(universeUUID && acknowlegedTasks?.[universeUUID] === bannerTaskId);

  // Status-only banners for tasks without a dedicated banner component.
  const renderGenericTaskBanner = (bannerTask: Task) => {
    switch (bannerTask.status) {
      case TaskState.RUNNING:
        return (
          <TaskInProgressBanner
            currentTask={bannerTask}
            viewDetails={() => {
              toggleTaskDetailsDrawer(true, bannerTask.id);
            }}
            onClose={noop}
          />
        );
      case TaskState.SUCCESS:
        return (
          <TaskSuccessBanner
            currentTask={bannerTask}
            viewDetails={() => {
              toggleTaskDetailsDrawer(true, bannerTask.id);
            }}
            onClose={() => hideBanner(bannerTask.id)}
          />
        );
      case TaskState.FAILURE:
        return (
          <TaskFailedBanner
            currentTask={bannerTask}
            viewDetails={() => {
              toggleTaskDetailsDrawer(true, bannerTask.id);
            }}
            onClose={() => hideBanner(bannerTask.id)}
          />
        );
      default:
        return null;
    }
  };

  const renderBannerForTask = (bannerTask: Task): JSX.Element | null => {
    if (isCanaryUpgradeEnabled) {
      if (getIsDbUpgradePrecheckTask(bannerTask)) {
        if (isBannerDismissedForTask(bannerTask.id)) {
          return null;
        }
        return (
          <div className={classes.bannerContainer}>
            <DbUpgradePrecheckTaskBanner
              task={bannerTask}
              universeUuid={universeUUID}
              onDismiss={() => hideBanner(bannerTask.id)}
            />
          </div>
        );
      }

      if (getIsDbUpgradeRollbackTask(bannerTask)) {
        return (
          <div className={classes.bannerContainer}>
            <DbUpgradeRollbackTaskBanner task={bannerTask} universeUuid={universeUUID} />
          </div>
        );
      }

      if (getIsDbUpgradeFinalizeTask(bannerTask)) {
        return (
          <div className={classes.bannerContainer}>
            <DbUpgradeFinalizeTaskBanner task={bannerTask} universeUuid={universeUUID} />
          </div>
        );
      }

      if (getIsDbUpgradeTask(bannerTask)) {
        return (
          <div className={classes.bannerContainer}>
            <DbUpgradeTaskBanner
              task={bannerTask}
              universeUuid={universeUUID}
              isUpgradeCompletedBannerDismissed={isBannerDismissedForTask(bannerTask.id)}
              onDismissUpgradeCompletedBanner={() => hideBanner(bannerTask.id)}
            />
          </div>
        );
      }
    }

    if (isBannerDismissedForTask(bannerTask.id)) {
      return null;
    }

    if (getIsEditUniverseRollbackTask(bannerTask)) {
      return (
        <div className={classes.bannerContainer}>
          <EditUniverseRollbackTaskBanner task={bannerTask} universeUuid={universeUUID} />
        </div>
      );
    }

    if (getIsEditUniverseTask(bannerTask)) {
      return (
        <div className={classes.bannerContainer}>
          <EditUniverseTaskBanner
            task={bannerTask}
            universeUuid={universeUUID}
            onDismiss={() => hideBanner(bannerTask.id)}
          />
        </div>
      );
    }

    if (
      bannerTask.status === TaskState.FAILURE &&
      isSoftwareUpgradeFailed(bannerTask, universeData)
    ) {
      return (
        <TaskFailedSoftwareUpgradeBanner
          currentTask={bannerTask}
          viewDetails={() => {
            toggleTaskDetailsDrawer(true, bannerTask.id);
          }}
          onClose={() => hideBanner(bannerTask.id)}
        />
      );
    }

    return renderGenericTaskBanner(bannerTask);
  };

  if (!isNewTaskDetailsUIEnabled) return null;

  if (universeUUID && lastCreatedTask?.targetUUID !== universeUUID) return null;

  if (!lastCreatedTask) return null;

  if (universeRuntimeConfigsQuery.isLoading) {
    return null;
  }

  if (isCanaryUpgradeEnabled) {
    const softwareUpgradeLockingTask = getLatestSoftwareUpgradeLockingTaskForUniverse(
      customerTaskList,
      universeUUID
    );

    // A newer non-locking task on the universe (support bundle, etc.) is latest.
    // In this case, we intentionally render task banners for both the latest task and
    // the software upgrade task.
    if (softwareUpgradeLockingTask && softwareUpgradeLockingTask.id !== lastCreatedTask.id) {
      return (
        <div className={classes.bannersContainer}>
          {renderBannerForTask(lastCreatedTask)}
          {renderBannerForTask(softwareUpgradeLockingTask)}
        </div>
      );
    }

    // Software upgrade and software upgrade pre-check tasks have their own banner components.
    if (
      getIsDbUpgradePrecheckTask(lastCreatedTask) ||
      getIsSoftwareUpgradeLockingTask(lastCreatedTask)
    ) {
      return renderBannerForTask(lastCreatedTask);
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
          {isBannerDismissedForTask(lastCreatedTask.id)
            ? null
            : renderGenericTaskBanner(lastCreatedTask)}
          {universeStatus.state === UniverseState.GOOD && (
            <>
              <div className={classes.bannerContainer}>
                <YBOperationBanner
                  variant={OperationBannerVariant.Warning}
                  dense
                  minHeight={46}
                  showDivider={false}
                  iconCircle={false}
                  icon={<OperationBannerWaveIcon />}
                  title={t('universeActions.dbUpgrade.clusterBanner.finalizeOrRollBack.title')}
                  action={
                    <YBButton
                      variant="secondary"
                      size="medium"
                      data-testid="open-upgrade-monitor-button"
                      onClick={() => {
                        setIsDbUpgradeManagementSidePanelOpen(true);
                      }}
                    >
                      {t(
                        'universeActions.dbUpgrade.clusterBanner.actions.openUpgradeMonitorToContinue'
                      )}
                    </YBButton>
                  }
                  message={t(
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

  return renderBannerForTask(lastCreatedTask);
};

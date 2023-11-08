import { useTranslation } from 'react-i18next';
import { useQuery, useQueryClient } from 'react-query';

import { api, taskQueryKey, universeQueryKey } from '../../../../redesign/helpers/api';
import { YBErrorIndicator, YBLoading } from '../../../common/indicators';
import { TaskListTable, TaskProgressContainer } from '../../../tasks';
import { TASK_SHORT_TIMEOUT } from '../../../tasks/constants';

interface UniverseTaskListProps {
  universeUuid: string;
  abortTask: (taskUuid: string) => void;
  hideTaskAbortModal: () => void;
  showTaskAbortModal: () => void;
  // Updates the universe data stored in Redux.
  refreshUniverseData: () => void;
  visibleModal: string;
}

const TRANSLATION_KEY_PREFIX = 'clusterDetail.taskList';

export const UniverseTaskList = ({
  universeUuid,
  abortTask,
  hideTaskAbortModal,
  showTaskAbortModal,
  refreshUniverseData,
  visibleModal
}: UniverseTaskListProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const queryClient = useQueryClient();
  const universeTasksQuery = useQuery(
    taskQueryKey.universe(universeUuid),
    () => api.fetchUniverseTasks(universeUuid),
    { refetchInterval: TASK_SHORT_TIMEOUT }
  );

  if (universeTasksQuery.isError) {
    return <YBErrorIndicator customErrorMessage={t('error.failedToFetchUniverseTaskList')} />;
  }
  if (universeTasksQuery.isLoading || universeTasksQuery.isIdle) {
    return <YBLoading />;
  }

  const updateCachedUniverseData = () => {
    queryClient.invalidateQueries(universeQueryKey.detail(universeUuid));
    refreshUniverseData();
  };

  const universeTasks = universeTasksQuery.data;
  const activeTasks = universeTasks.reduce((activeTasks: any, task: any) => {
    if (isTaskRunning(task)) {
      activeTasks.push(task.id);
    }
    return activeTasks;
  }, []);
  return (
    <div className="universe-detail-content-container">
      <TaskProgressContainer
        taskUUIDs={activeTasks}
        type="StepBar"
        timeoutInterval={TASK_SHORT_TIMEOUT}
        onTaskSuccess={updateCachedUniverseData}
      />
      <TaskListTable
        taskList={universeTasks ?? []}
        title={t('taskHistory.title')}
        abortTask={abortTask}
        hideTaskAbortModal={hideTaskAbortModal}
        showTaskAbortModal={showTaskAbortModal}
        visibleModal={visibleModal}
      />
    </div>
  );
};

const isTaskRunning = (task: any) =>
  task.status !== 'Aborted' && task.status !== 'Failure' && task.percentComplete !== 100;

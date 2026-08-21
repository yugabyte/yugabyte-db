import { useMemo, type ComponentType } from 'react';
import { Provider } from 'react-redux';
import { applyMiddleware, combineReducers, createStore, type Store } from 'redux';
import promise from 'redux-promise';

import { type Task } from '@app/redesign/features/tasks/dtos';
import tasksReducer from '../../reducers/reducer_tasks';

const tasksRootReducer = combineReducers({
  tasks: tasksReducer
});

export type StorybookTasksRootState = ReturnType<typeof tasksRootReducer>;

const STORYBOOK_NOOP_ACTION = { type: '__STORYBOOK_NOOP__' } as const;

/** Preloads `tasks.customerTaskList` for Storybook; omit or pass `[]` for an empty list. */
export function createStorybookTasksRootStore(customerTasks?: Task[]): Store<StorybookTasksRootState> {
  const baseTasksState = tasksReducer(undefined, STORYBOOK_NOOP_ACTION as never);
  const preloadedState: StorybookTasksRootState = {
    tasks: {
      ...baseTasksState,
      customerTaskList: customerTasks ?? [],
      showTaskInDrawer: ''
    }
  };
  return createStore(tasksRootReducer, preloadedState, applyMiddleware(promise));
}

function StorybookTasksReduxShell({ Story }: { Story: ComponentType }) {
  const store = useMemo(() => createStorybookTasksRootStore(), []);

  return (
    <Provider store={store}>
      <Story />
    </Provider>
  );
}

export function withStorybookTasksReduxProvider(Story: ComponentType) {
  return <StorybookTasksReduxShell Story={Story} />;
}

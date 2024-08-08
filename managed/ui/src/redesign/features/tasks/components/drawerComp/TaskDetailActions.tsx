/*
 * Created on Wed Dec 20 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { useMutation } from 'react-query';
import { toast } from 'react-toastify';
import { useDispatch } from 'react-redux';
import { useToggle } from 'react-use';
import { useTranslation } from 'react-i18next';
import { makeStyles } from '@material-ui/core';
import { YBButton, YBModal } from '../../../../components';
import TaskDiffModal from '../TaskDiffModal';
import { fetchCustomerTasks } from '../../../../../actions/tasks';
import { fetchUniverseInfo, fetchUniverseInfoResponse } from '../../../../../actions/universe';
import { abortTask, retryTasks } from './api';
import { doesTaskSupportsDiffData } from '../../TaskUtils';
import { TaskDrawerCompProps } from './dtos';

const useStyles = makeStyles(() => ({
  root: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'flex-end',
    gap: '12px'
  }
}));

export const TaskDetailActions: FC<TaskDrawerCompProps> = ({ currentTask }) => {
  const [showAbortConfirmationModal, toggleAbortConfirmationModal] = useToggle(false);
  const [showRetryConfirmationModal, toggleRetryConfirmationModal] = useToggle(false);
  const [showTaskDiffModal, toggleTaskDiffModal] = useToggle(false);

  const { t } = useTranslation('translation', {
    keyPrefix: 'taskDetails.actions'
  });

  const classes = useStyles();

  const dispatch = useDispatch();

  // we should refresh the universe info after retrying the task, else the old task banner will be shown
  const refreshUniverse = ()=> {
    return dispatch(fetchUniverseInfo(currentTask.targetUUID) as any).then((response:any) => {
      return dispatch(fetchUniverseInfoResponse(response.payload));
    });
  };

  const doRetryTask = useMutation(() => retryTasks(currentTask?.id), {
    onSuccess: () => {
      toast.success(t('messages.taskRetrySuccess'));
    },
    onError: () => {
      toast.error(t('messages.taskRetryFailed'));
    },
    onSettled: () => {
      toggleRetryConfirmationModal(false);
      refreshUniverse();
      dispatch(fetchCustomerTasks());
    }
  });

  const doabortTask = useMutation(() => abortTask(currentTask?.id), {
    onSuccess: () => {
      toast.success(t('messages.taskAbortSuccess'));
    },
    onError: () => {
      toast.error(t('messages.taskAbortFailed'));
    },
    onSettled: () => {
      toggleAbortConfirmationModal(false);
      refreshUniverse();
      dispatch(fetchCustomerTasks());
    }
  });

  return (
    <div className={classes.root}>
      {doesTaskSupportsDiffData(currentTask) && (
        <YBButton
          data-testid="toggle-task-diff-modal"
          variant="secondary"
          onClick={() => {
            toggleTaskDiffModal(true);
          }}
        >
          {t(`diffs.${currentTask?.type}`)}
        </YBButton>
      )}
      {currentTask?.retryable && (
        <YBButton
          variant="secondary"
          onClick={() => {
            toggleRetryConfirmationModal(true);
          }}
          data-testid="retry-task"
        >
          {t('retry')}
        </YBButton>
      )}
      {currentTask?.abortable && (
        <YBButton
          variant="secondary"
          onClick={() => {
            toggleAbortConfirmationModal(true);
          }}
          data-testid="abort-task"
        >
          {t('abort')}
        </YBButton>
      )}
      <AbortConfirmModal
        visible={showAbortConfirmationModal}
        onClose={() => toggleAbortConfirmationModal(false)}
        onSubmit={() => doabortTask.mutate()}
      />
      <RetryConfirmModal
        visible={showRetryConfirmationModal}
        onClose={() => toggleRetryConfirmationModal(false)}
        onSubmit={() => doRetryTask.mutate()}
      />
      <TaskDiffModal
        visible={showTaskDiffModal}
        onClose={() => toggleTaskDiffModal(false)}
        currentTask={currentTask}
      />
    </div>
  );
};

interface ConfirmationModalProps {
  visible: boolean;
  onClose: () => void;
  onSubmit: () => void;
}

const AbortConfirmModal: FC<ConfirmationModalProps> = ({ visible, onClose, onSubmit }) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'taskDetails.actions'
  });

  if (!visible) return null;

  return (
    <YBModal
      open={visible}
      onClose={onClose}
      onSubmit={onSubmit}
      submitLabel={t('abort')}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      size="xs"
      title={t('messages.abortConfirmTitle')}
    >
      {t('messages.abortConfirmMsg')}
    </YBModal>
  );
};

const RetryConfirmModal: FC<ConfirmationModalProps> = ({ visible, onClose, onSubmit }) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'taskDetails.actions'
  });

  if (!visible) return null;
  return (
    <YBModal
      open={visible}
      onClose={onClose}
      onSubmit={onSubmit}
      submitLabel={t('retry')}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      size="xs"
      title={t('messages.retryConfirmTitle')}
    >
      {t('messages.retryConfirmMsg')}
    </YBModal>
  );
};

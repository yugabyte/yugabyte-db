/*
 * Created on Wed May 15 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useEffect, useMemo, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';
import { YBModal } from '../../../components';
import { BaseDiff } from './diffComp/diffs/BaseDiff';
import GFlagsDiff from './diffComp/diffs/GFlagsDiff';
import SoftwareUpgradeDiff from './diffComp/diffs/SoftwareUpgradeDiff';
import UniverseDiff from './diffComp/diffs/UniverseDiff';
import { getTaskDiffDetails } from './diffComp/api';
import { TargetType, Task, TaskType } from '../dtos';
import { DiffComponentProps } from './diffComp/dtos';
import { toast } from 'react-toastify';

interface TaskDiffModalProps {
  visible: boolean;
  onClose: () => void;
  currentTask: Task | null;
}

const TaskDiffModal: React.FC<TaskDiffModalProps> = ({ visible, onClose, currentTask }) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'taskDetails.diffModal'
  });

  // Differ to be used for the current task.
  const [differ, setDiffer] = useState<BaseDiff<DiffComponentProps, any> | null>(null);

  const { data: taskDiffDetails } = useQuery(
    ['taskDiffDetails', currentTask?.id],
    () => getTaskDiffDetails(currentTask!.id),
    {
      enabled: !!currentTask && visible,
      select: (data) => data.data,
      // old task won't have diff details
      onError: () => {
        toast.error(t('diffDetailsNotFound'));
      }
    }
  );

  useEffect(() => {
    if (!currentTask || !visible || !taskDiffDetails) {
      return;
    }

    // Set the differ based on the task type.

    if (currentTask.target === TargetType.UNIVERSE && currentTask.type === TaskType.EDIT) {
      setDiffer(new UniverseDiff({ ...taskDiffDetails, task: currentTask }));
    }
    if (
      currentTask.target === TargetType.UNIVERSE &&
      currentTask.type === TaskType.GFlags_UPGRADE
    ) {
      setDiffer(new GFlagsDiff({ ...taskDiffDetails, task: currentTask }));
    }
    if (
      currentTask.target === TargetType.UNIVERSE &&
      currentTask.type === TaskType.SOFTWARE_UPGRADE
    ) {
      setDiffer(new SoftwareUpgradeDiff({ ...taskDiffDetails, task: currentTask }));
    }
  }, [currentTask, visible, taskDiffDetails]);

  // Get the diff component to be rendered.
  // memoize to avoid re-rendering on every state change.
  const diffComponents = useMemo(() => {
    if (!differ) {
      return null;
    }
    return differ.getDiffComponent();
  }, [differ]);

  if (!currentTask || !visible || !taskDiffDetails) {
    return null;
  }

  return (
    <YBModal
      open={visible}
      onClose={onClose}
      title={t(`${currentTask?.type}`, { keyPrefix: 'taskDetails.diffModal.titles' })}
      overrideWidth={'900px'}
      overrideHeight={'auto'}
      titleSeparator
      enableBackdropDismiss
      dialogContentProps={{ dividers: true, style: { padding: '20px' } }}
    >
      {diffComponents}
    </YBModal>
  );
};

export default TaskDiffModal;

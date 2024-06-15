import { animals, colors, Config, uniqueNamesGenerator } from 'unique-names-generator';
import { positiveAdjectives } from './dictionaries';
import { isEmptyArray } from '../../utils/ObjectUtils';
import { AllowedTasks } from './dtos';
import { UNIVERSE_ACTION_TO_FROZEN_TASK_MAP } from './constants';

const defaultConfig: Config = {
  dictionaries: [positiveAdjectives, colors, animals],
  separator: '-',
  length: 3
};

export const isActionFrozen = (allowedTasks: AllowedTasks, action: string) => {
  if (!allowedTasks) {
    return false;
  }

  const frozenAction = UNIVERSE_ACTION_TO_FROZEN_TASK_MAP[action];
  const isRestricted = allowedTasks.restricted;
  const taskIds = allowedTasks.taskIds;

  return isRestricted && (isEmptyArray(taskIds) || !taskIds.includes(frozenAction));
};

export const generateUniqueName = (config?: Config): string =>
  uniqueNamesGenerator(config ?? defaultConfig);

export const getMemorySizeUnits = (bytes: number): string => {
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
  if (bytes === 0) return '0 B';
  if (bytes === null || isNaN(bytes)) return '-';
  const i = parseInt(String(Math.floor(Math.log(bytes) / Math.log(1024))));
  return `${Math.round(bytes / Math.pow(1024, i))} ${sizes[i]}`;
};

import { animals, colors, Config, uniqueNamesGenerator } from 'unique-names-generator';
import { positiveAdjectives } from './dictionaries';

const defaultConfig: Config = {
  dictionaries: [positiveAdjectives, colors, animals],
  separator: '-',
  length: 3
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

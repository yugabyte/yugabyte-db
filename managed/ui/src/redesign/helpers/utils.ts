import { animals, colors, Config, uniqueNamesGenerator } from 'unique-names-generator';
import { positiveAdjectives } from './dictionaries';

const defaultConfig: Config = {
  dictionaries: [positiveAdjectives, colors, animals],
  separator: '-',
  length: 3
};

export const generateUniqueName = (config?: Config): string =>
  uniqueNamesGenerator(config ?? defaultConfig);

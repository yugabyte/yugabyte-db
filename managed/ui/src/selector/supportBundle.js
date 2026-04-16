import { get } from 'lodash';

export const getSupportBundles = (state) => {
  const supportBundles = get(state, 'supportBundle.supportBundle.data', []);
  return [
    supportBundles,
    (index) => {
      return supportBundles[index];
    }
  ];
};

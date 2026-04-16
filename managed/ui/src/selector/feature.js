import { get } from 'lodash';

export const getFeatureFromTest = (state) => {
  const featuresInTest = get(state, 'featureFlags.test', {});
  return [
    featuresInTest,
    (feature) => {
      return featuresInTest[feature];
    }
  ];
};

export const getFeatureFromReleased = (state) => {
  const featuresInReleased = get(state, 'featureFlags.released', {});
  return [
    featuresInReleased,
    (feature) => {
      return featuresInReleased[feature];
    }
  ];
};

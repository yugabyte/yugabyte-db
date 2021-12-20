import { TOGGLE_FEATURE } from '../actions/feature';

const initialStateFeatureInTest = {
  pausedUniverse: false,
  addListMultiProvider: false,
  adminAlertsConfig: true,
  enableNewEncryptionInTransitModal: true,
  addRestoreTimeStamp: false,
  enableXCluster: false,
  enableGeoPartitioning: false,
  enableHCVault: false,
  enableNodeComparisonModal: false
};

const initialStateFeatureReleased = {
  pausedUniverse: true,
  addListMultiProvider: true,
  adminAlertsConfig: true,
  enableNewEncryptionInTransitModal: true,
  addRestoreTimeStamp: false,
  enableXCluster: true,
  enableGeoPartitioning: false,
  enableHCVault: false,
  enableNodeComparisonModal: false
};

export const FeatureFlag = (
  state = {
    test: { ...initialStateFeatureInTest, ...initialStateFeatureReleased },
    released: initialStateFeatureReleased
  },
  action
) => {
  switch (action.type) {
    case TOGGLE_FEATURE:
      if (!state.released[action.feature]) {
        state.test[action.feature] = !state.test[action.feature];
      }
      return { ...state, test: { ...state.test } };
    default:
      return state;
  }
};

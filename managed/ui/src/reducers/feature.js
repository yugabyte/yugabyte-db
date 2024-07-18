import { mapValues } from 'lodash';
import { TOGGLE_FEATURE } from '../actions/feature';

const initialStateFeatureInTest = {
  pausedUniverse: false,
  addListMultiProvider: false,
  adminAlertsConfig: true,
  enableNewEncryptionInTransitModal: true,
  addRestoreTimeStamp: false,
  enableXCluster: false,
  enableTroubleshooting: false,
  enableHCVault: true,
  enableHCVaultEAT: true,
  enableNodeComparisonModal: false,
  enablePathStyleAccess: false,
  backupv2: false,
  enableOIDC: true,
  supportBundle: false,
  enableThirdpartyUpgrade: false,
  enableYbc: true,
  enableMultiRegionConfig: false,
  enableGcpKMS: true,
  enableAzuKMS: true,
  enableRunTimeConfig: true,
  enablePITR: true,
  enableNotificationTemplates: false,
  enableRestore: true,
  enablePrefillKubeConfig: true,
  enableNewUI: true, // feature flag to enable new revamped UI,
  enableCustomEmailTemplates: true,
  enableAWSProviderValidation: true,
  enableMKR: true,
  enableS3BackupProxy: false,
  enableRRGflags: true,
  enableLDAPRoleMapping: true,
  enableNewRestoreModal: true,
  enableCACertRotation: true,
  enableNewAdvancedRestoreModal: false,
  showReplicationSlots: true,
  newTaskDetailsUI: false
};

const initialStateFeatureReleased = {
  pausedUniverse: true,
  addListMultiProvider: true,
  adminAlertsConfig: true,
  enableNewEncryptionInTransitModal: true,
  addRestoreTimeStamp: false,
  enableXCluster: true,
  enableTroubleshooting: false,
  enableHCVault: true,
  enableHCVaultEAT: true,
  enableNodeComparisonModal: false,
  enablePathStyleAccess: false,
  backupv2: true,
  enableOIDC: true,
  supportBundle: true,
  enableThirdpartyUpgrade: false,
  enableYbc: true,
  enableMultiRegionConfig: false,
  enableGcpKMS: true,
  enableAzuKMS: true,
  enableRunTimeConfig: true,
  enablePITR: true,
  enableNotificationTemplates: false,
  enableRestore: true,
  enablePrefillKubeConfig: true,
  enableCustomEmailTemplates: true
  // enableRRGflags: true
};

export const testFeatureFlagsLocalStorageKey = 'featureFlags-test';

//Get feature flags from the local storage
const featureFlagsInLocalStorage = JSON.parse(
  localStorage.getItem(testFeatureFlagsLocalStorageKey) ?? '{}'
);

//Rather than directly utilizing the values stored in the local storage, we opt to map them to the test feature flag.
//This approach ensures that we do not overwrite any newly added values in the test feature flag
const testFeatureFlags = mapValues(
  initialStateFeatureInTest,
  (val, key) => featureFlagsInLocalStorage[key] ?? val
);

export const FeatureFlag = (
  state = {
    test: { ...testFeatureFlags, ...initialStateFeatureReleased },
    released: initialStateFeatureReleased
  },
  action
) => {
  switch (action.type) {
    case TOGGLE_FEATURE:
      if (!state.released[action.feature]) {
        state.test[action.feature] = !state.test[action.feature];
      }
      localStorage.setItem(testFeatureFlagsLocalStorageKey, JSON.stringify(state.test));
      return { ...state, test: { ...state.test } };
    default:
      return state;
  }
};

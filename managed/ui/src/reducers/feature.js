import { TOGGLE_FEATURE } from '../actions/feature';

const initialStateFeatureInTest = {
  pausedUniverse: false,
  addListMultiProvider: false,
  adminAlertsConfig: true,
  enableNewEncryptionInTransitModal: true,
  addRestoreTimeStamp: false,
  enableXCluster: false,
  enableGeoPartitioning: false,
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
  enableGFlagMultilineConf: false,
  enableMKR: true,
  enableS3BackupProxy: false,
  enableRRGflags: true,
  enableLDAPRoleMapping: true,
  enableNewRestoreModal: false,
  enableConfigureDBApi: false
};

const initialStateFeatureReleased = {
  pausedUniverse: true,
  addListMultiProvider: true,
  adminAlertsConfig: true,
  enableNewEncryptionInTransitModal: true,
  addRestoreTimeStamp: false,
  enableXCluster: true,
  enableGeoPartitioning: false,
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
  enableCustomEmailTemplates: true,
  enableGFlagMultilineConf: false
  // enableRRGflags: true
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

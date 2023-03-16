import _ from 'lodash';
import { makeStyles } from '@material-ui/core';
import { KmsConfig } from '../../universe-form/utils/dto';

//styles
export const useMKRStyles = makeStyles((theme) => ({
  container: {
    backgroundColor: theme.palette.ybacolors.ybBackgroundGray,
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: theme.spacing(1)
  },
  subContainer: {
    padding: theme.spacing(3)
  },
  universeKeyContainer: {
    padding: theme.spacing(0.5, 1),
    backgroundColor: theme.palette.background.paper
  },
  enableEARContainer: {
    padding: theme.spacing(2)
  },
  kmsField: {
    '& .MuiInputLabel-root': {
      textTransform: 'unset',
      fontWeight: 400,
      fontSize: 14
    }
  },
  chip: {
    marginLeft: theme.spacing(2)
  },
  rotationInfoText: {
    fontSize: 12,
    color: theme.palette.grey[600]
  }
}));

// consts
export const KMS_FIELD_NAME = 'kmsConfigUUID';
export const UNIVERSE_KEY_FIELD_NAME = 'rotateUniverseKey';
export const EAR_FIELD_NAME = 'encryptionAtRestEnabled';
export const TOAST_AUTO_DISMISS_INTERVAL = 3000;

// dtos
export interface EncryptionAtRestFormValues {
  encryptionAtRestEnabled: boolean;
  kmsConfigUUID?: string;
  rotateUniverseKey: boolean;
}

export interface KMSHistory {
  configUUID: string;
  reference: string;
  timestamp: string;
  configName?: string;
}

export type KMSRotationHistory = KMSHistory[];

export interface RotationInfo {
  masterKey: KMSHistory | null;
  universeKey: KMSHistory | null;
  lastActiveKey: KMSHistory | null;
}

//helpers
export const getLastRotationDetails = (
  kms_history: KMSRotationHistory,
  kms_configs: KmsConfig[]
) => {
  let kmsHistory: KMSHistory[] = kms_history.map((history: KMSHistory) => ({
    ...history,
    unixTimeStamp: new Date(history.timestamp).getTime(),
    configName: kms_configs.find(
      (config: KmsConfig) => config.metadata.configUUID === history.configUUID
    )?.metadata?.name
  }));
  kmsHistory = _.orderBy(kmsHistory, ['unixTimeStamp'], ['desc']);

  const lastKeyRotatons: RotationInfo = {
    masterKey: null,
    universeKey: null,
    lastActiveKey: null
  };

  if (kmsHistory.length) {
    const lastActiveKey = kmsHistory[0];
    lastKeyRotatons.lastActiveKey = lastActiveKey;

    if (kmsHistory.length > 1) {
      //MKR
      const mkrRotationIndex = kmsHistory.findIndex(
        (history: KMSHistory) => history.configUUID !== lastActiveKey.configUUID
      );
      if (mkrRotationIndex > -1) lastKeyRotatons.masterKey = kmsHistory[mkrRotationIndex - 1];

      //Universe Key Rotation
      const ukRotationIndex = kmsHistory.findIndex(
        (history: KMSHistory, index: number) =>
          index > 0 && history.configUUID === kmsHistory[index - 1]?.configUUID
      );
      if (ukRotationIndex) lastKeyRotatons.universeKey = kmsHistory[ukRotationIndex - 1];
    }
  }

  return lastKeyRotatons;
};

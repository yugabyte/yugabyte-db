import _ from 'lodash';
import { makeStyles } from '@material-ui/core';
import { KmsConfig } from '../../universe-form/utils/dto';

//styles
export const useMKRStyles = makeStyles((theme) => ({
  container: {
    backgroundColor: theme.palette.ybacolors.backgroundGrayLightest,
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
  re_encryption_count: number;
}

export type KMSRotationHistory = KMSHistory[];

export interface RotationInfo {
  masterKey: KMSHistory | null;
  universeKey: KMSHistory | null;
  lastActiveKey: KMSHistory | null;
}
const ROTATION_INFO_DEFAULT_VALUES = {
  masterKey: null,
  universeKey: null,
  lastActiveKey: null
};

const transformKMSHistory = (kms_history: KMSRotationHistory, kms_configs: KmsConfig[]) => {
  let kmsHistory: KMSHistory[] = kms_history.map((history: KMSHistory) => ({
    ...history,
    unixTimeStamp: new Date(history.timestamp).getTime(),
    configName: kms_configs.find(
      (config: KmsConfig) => config.metadata.configUUID === history.configUUID
    )?.metadata?.name
  }));
  kmsHistory = _.orderBy(kmsHistory, ['unixTimeStamp'], ['desc']);
  return kmsHistory;
};

//helpers
export const getLastRotationDetails = (
  kms_history: KMSRotationHistory,
  kms_configs: KmsConfig[]
) => {
  let kmsHistory: KMSHistory[] = transformKMSHistory(kms_history, kms_configs);
  const rotationInfo: RotationInfo = { ...ROTATION_INFO_DEFAULT_VALUES };

  const findUKRInfo = (currentReEncryptionCount: number) => {
    const prevReEncryptionCount = currentReEncryptionCount - 1;
    //find first 2 sets of entries with different re encryption count
    const currentReEncryptionEntries = kmsHistory.filter(
      (history: KMSHistory) => history.re_encryption_count === currentReEncryptionCount
    );
    const prevReEncryptionEntries = kmsHistory.filter(
      (history: KMSHistory) => history.re_encryption_count === prevReEncryptionCount
    );

    if (currentReEncryptionEntries.length > prevReEncryptionEntries.length) {
      //ukr was done on the highest re encryption count
      rotationInfo.universeKey = currentReEncryptionEntries[0];
    } else if (currentReEncryptionCount > 1 && !rotationInfo.universeKey) {
      //recursively call it for next set
      findUKRInfo(currentReEncryptionCount - 1);
    } else if (
      prevReEncryptionCount === 0 &&
      !rotationInfo.universeKey &&
      prevReEncryptionEntries.length > 1
    ) {
      rotationInfo.universeKey = prevReEncryptionEntries[0];
    }
  };

  if (kmsHistory.length == 0) return rotationInfo; //No history

  //START - Last Active Key ------------------------------------------------------------------------------------
  const lastActiveKey = kmsHistory[0];
  rotationInfo.lastActiveKey = lastActiveKey;
  //END - Last Active Key ----- --------------------------------------------------------------------------------

  if (kmsHistory.length <= 1) return rotationInfo; //No rotations

  //START - Master Key Rotation---------------------------------------------------------------------------------
  //loop through history -> compare each item with last active kms -> find at which place re_encryption_count is changed
  const mkrRotationIndex = kmsHistory.findIndex(
    (history: KMSHistory) => history.re_encryption_count !== lastActiveKey.re_encryption_count
  );
  if (mkrRotationIndex > -1) rotationInfo.masterKey = kmsHistory[mkrRotationIndex - 1];
  //END - Master Key Rotation -----------------------------------------------------------------------------------

  //START - Universe Key Rotation -------------------------------------------------------------------------------
  if (mkrRotationIndex > -1) {
    //recusive function to find last UKR info
    findUKRInfo(kmsHistory[mkrRotationIndex - 1].re_encryption_count);
  } else {
    //if no MKRs performed
    rotationInfo.universeKey = lastActiveKey;
  }
  //END - Universe Key Rotation --------------------------------------------------------------------------------

  return rotationInfo;
};

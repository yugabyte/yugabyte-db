import {
  getPrimaryCluster,
  getAsyncCluster,
  transformSpecificGFlagToFlagsArray,
  transformGFlagToFlagsArray
} from '../../universe-form/utils/helpers';
import { Universe, Gflag, Cluster } from '../../universe-form/utils/dto';

export enum UpgradeOptions {
  Rolling = 'Rolling',
  NonRolling = 'Non-Rolling',
  NonRestart = 'Non-Restart'
}

export interface EditGflagsFormValues {
  gFlags: Gflag[] | [];
  inheritFlagsFromPrimary?: boolean;
  asyncGflags: Gflag[] | [];
  timeDelay: number;
  upgradeOption: UpgradeOptions;
}

export interface EditGflagPayload {
  clusters: Cluster[];
  nodePrefix: string;
  sleepAfterMasterRestartMillis: number;
  sleepAfterTServerRestartMillis: number;
  taskType: string;
  universeUUID: string;
  upgradeOption: UpgradeOptions;
  ybSoftwareVersion: string;
}

export const transformToEditFlagsForm = (universeData: Universe) => {
  const { universeDetails } = universeData;
  const editGflagsFormData: Partial<EditGflagsFormValues> = {
    gFlags: [],
    asyncGflags: [],
    inheritFlagsFromPrimary: true
  };
  const primaryCluster = getPrimaryCluster(universeDetails);
  const asyncCluster = getAsyncCluster(universeDetails);
  if (primaryCluster) {
    const {
      userIntent: { specificGFlags, masterGFlags, tserverGFlags }
    } = primaryCluster;
    if (specificGFlags)
      editGflagsFormData['gFlags'] = transformSpecificGFlagToFlagsArray(specificGFlags);
    else editGflagsFormData['gFlags'] = transformGFlagToFlagsArray(masterGFlags, tserverGFlags);
  }
  if (asyncCluster) {
    const {
      userIntent: { specificGFlags }
    } = asyncCluster;
    if (specificGFlags) {
      editGflagsFormData['asyncGflags'] = transformSpecificGFlagToFlagsArray(specificGFlags);
      editGflagsFormData['inheritFlagsFromPrimary'] = specificGFlags?.inheritFromPrimary;
    }
  }
  return editGflagsFormData;
};

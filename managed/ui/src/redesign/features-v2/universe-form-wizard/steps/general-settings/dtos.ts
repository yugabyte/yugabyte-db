import {
  UniverseInfo,
  UniverseSpec
} from '../../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';

export interface GeneralSettingsProps {
  universeName: UniverseSpec['name'];
  providerConfiguration: {
    uuid: string;
    isOnPremManuallyProvisioned: boolean;
    code: string;
  };
  databaseVersion: UniverseInfo['ybc_software_version'];
  cloud: string;
}

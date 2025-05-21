import {
  UniverseInfo,
  UniverseSpec,
  UniverseNetworkingSpec
} from '../../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';

export interface SecuritySettingsProps {
  assignPublicIP?: UniverseNetworkingSpec['assign_public_ip'];
  enableEncryptionAtRest?: boolean;
  kmsConfig?: string;
}

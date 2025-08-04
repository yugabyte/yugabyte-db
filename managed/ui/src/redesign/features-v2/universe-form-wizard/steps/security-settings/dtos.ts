import {
  UniverseInfo,
  UniverseSpec,
  UniverseNetworkingSpec
} from '../../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';

export interface SecuritySettingsProps {
  assignPublicIP?: UniverseNetworkingSpec['assign_public_ip'];
  enableEncryptionAtRest?: boolean;
  kmsConfig?: string;
  //Same certicate
  useSameCertificate?: boolean;
  enableBothEncryption?: boolean;
  rootCertificate?: string;
  generateCerticate?: string;
  //Node to Node
  enableNodeToNodeEncryption?: boolean;
  rootNToNCertificate?: string;
  generateNToNCertiacte?: boolean;
  //Client to Node
  enableClientToNodeEncryption?: boolean;
  rootCToNCertificate?: string;
  generateCToNCertificate?: boolean;
}

import {
  UniverseInfo,
  UniverseSpec,
  UniverseNetworkingSpec
} from '../../../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';

export enum CertType {
  SELF_SIGNED = 'SelfSigned',
  CUSTOM = 'Custom'
}

export interface SecuritySettingsProps {
  assignPublicIP?: UniverseNetworkingSpec['assign_public_ip'];
  enableIPV6: boolean;
  enableEncryptionAtRest?: boolean;
  kmsConfig?: string;
  //Same certicate
  useSameCertificate?: boolean;
  enableBothEncryption?: boolean;
  rootCertificate?: string;
  certType?: CertType;
  //Node to Node
  enableNodeToNodeEncryption?: boolean;
  rootNToNCertificate?: string;
  certTypeNtoN?: CertType;
  //Client to Node
  enableClientToNodeEncryption?: boolean;
  rootCToNCertificate?: string;
  certTypeCToN?: CertType;
}

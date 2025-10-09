import { ImageBundle } from '../../../../../../components/configRedesign/providerRedesign/types';
import {
  UniverseInfo,
  UniverseSpec
} from '../../../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { AccessKey, CloudType } from '../../../../../features/universe/universe-form/utils/dto';
import {
  CLOUD,
  DATABASE_VERSION,
  PROVIDER_CONFIGURATION,
  UNIVERSE_NAME
} from '../../fields/FieldNames';

export interface ProviderType {
  uuid: string;
  isOnPremManuallyProvisioned: boolean;
  code: CloudType;
  imageBundles: ImageBundle & { uuid: string }[];
  allAccessKeys: AccessKey[];
}
export interface GeneralSettingsProps {
  [UNIVERSE_NAME]: UniverseSpec['name'];
  [PROVIDER_CONFIGURATION]: ProviderType;
  [DATABASE_VERSION]: UniverseInfo['ybc_software_version'];
  [CLOUD]: string;
}

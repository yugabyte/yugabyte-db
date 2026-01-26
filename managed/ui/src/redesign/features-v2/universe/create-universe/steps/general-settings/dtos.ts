import { ArchitectureType } from '@app/components/configRedesign/providerRedesign/constants';
import {
  ImageBundle,
  Provider
} from '../../../../../../components/configRedesign/providerRedesign/types';
import {
  UniverseInfo,
  UniverseSpec
} from '../../../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { CloudType } from '../../../../../features/universe/universe-form/utils/dto';
import {
  CLOUD,
  DATABASE_VERSION,
  PROVIDER_CONFIGURATION,
  UNIVERSE_NAME
} from '../../fields/FieldNames';

export interface ProviderType extends Provider {
  uuid: string;
  isOnPremManuallyProvisioned: boolean;
  code: CloudType;
  imageBundles: ImageBundle & { uuid: string; details: { arch: ArchitectureType } }[];
}
export interface GeneralSettingsProps {
  [UNIVERSE_NAME]: UniverseSpec['name'];
  [PROVIDER_CONFIGURATION]: ProviderType;
  [DATABASE_VERSION]: UniverseInfo['ybc_software_version'];
  [CLOUD]: string;
}

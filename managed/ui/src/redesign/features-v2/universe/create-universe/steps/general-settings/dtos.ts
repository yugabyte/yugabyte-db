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
  PROVIDER_CONFIGURATIONS,
  SPAN_ACROSS_PROVIDERS,
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
  /** Ordered unique provider configs (1–3). Selection order = cloud-card order. */
  [PROVIDER_CONFIGURATIONS]: ProviderType[];
  [DATABASE_VERSION]: UniverseInfo['ybc_software_version'];
  /**
   * Cloud-type selection via YBCloudSelect. `string` when Span is off;
   * `string[]` when Span is on (max 3, one config per code).
   */
  [CLOUD]: string | string[];
  /** Multi-provider General UI mode (ignored when ENABLE_MULTI_PROVIDER_CREATE is false). */
  [SPAN_ACROSS_PROVIDERS]?: boolean;
}

import { ImageBundle } from '../../../../../components/configRedesign/providerRedesign/types';
import { UniverseInfo, UniverseSpec } from '../../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { AccessKey } from '../../../../features/universe/universe-form/utils/dto';
import {
  CLOUD,
  DATABASE_VERSION,
  PROVIDER_CONFIGURATION,
  UNIVERSE_NAME
} from '../../fields/FieldNames';

export interface GeneralSettingsProps {
  [UNIVERSE_NAME]: UniverseSpec['name'];
  [PROVIDER_CONFIGURATION]: {
    uuid: string;
    isOnPremManuallyProvisioned: boolean;
    code: string;
    imageBundles: ImageBundle & { uuid: string }[];
    allAccessKeys: AccessKey[];
  };
  [DATABASE_VERSION]: UniverseInfo['ybc_software_version'];
  [CLOUD]: string;
}

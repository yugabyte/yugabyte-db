import { RunTimeConfig } from '@app/redesign/features/universe/universe-form/utils/dto';
import { RuntimeConfigKey } from '@app/redesign/helpers/constants';

export const isV2CreateEditUniverseEnabled = (runtimeConfigs: RunTimeConfig) => {
  return (
    runtimeConfigs?.configEntries?.find(
      (config) => config.key === RuntimeConfigKey.ENABLE_V2_EDIT_UNIVERSE_UI
    )?.value === 'true'
  );
};

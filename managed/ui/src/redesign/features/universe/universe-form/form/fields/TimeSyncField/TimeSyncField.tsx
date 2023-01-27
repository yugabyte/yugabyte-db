import React, { ReactElement } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext, useWatch } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBLabel, YBHelper, YBToggleField } from '../../../../../../components';
import { CloudType, UniverseFormData } from '../../../utils/dto';
import { TIME_SYNC_FIELD, PROVIDER_FIELD } from '../../../utils/constants';

interface TimeSyncFieldProps {
  disabled: boolean;
}

const PROVIDER_FRIENDLY_NAME = {
  [CloudType.aws]: 'AWS',
  [CloudType.gcp]: 'GCP',
  [CloudType.azu]: 'Azure'
};

export const TimeSyncField = ({ disabled }: TimeSyncFieldProps): ReactElement => {
  const { control } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();

  //watchers
  const provider = useWatch({ name: PROVIDER_FIELD });

  const stringMap = { provider: PROVIDER_FRIENDLY_NAME[provider?.code] };

  return (
    <Box display="flex" width="100%" data-testid="TimeSyncField-Container">
      <YBLabel dataTestId="TimeSyncField-Label">
        {t('universeForm.instanceConfig.useTimeSync', stringMap)}
      </YBLabel>
      <Box flex={1}>
        <YBToggleField
          name={TIME_SYNC_FIELD}
          inputProps={{
            'data-testid': 'TimeSyncField-Toggle'
          }}
          control={control}
          disabled={disabled}
        />
        <YBHelper dataTestId="TimeSyncField-Helper">
          {t('universeForm.instanceConfig.useTimeSyncHelper', stringMap)}
        </YBHelper>
      </Box>
    </Box>
  );
};

//shown only for aws, gcp, azu
//show only if current access key's setupchrony = false

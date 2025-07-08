import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { mui, YBCheckboxField } from '@yugabyte-ui-library/core';
import { OtherAdvancedProps } from '../../steps/advanced-settings/dtos';
import { CloudType } from '../../../../helpers/dtos';
import { QUERY_KEY, api } from '../../../../features/universe/universe-form/utils/api';
import { useQuery } from 'react-query';
import { ProviderConfig } from '../../steps/general-settings/dtos';

const { Box } = mui;

interface TimeSyncProps {
  disabled: boolean;
  provider: ProviderConfig;
}

const PROVIDER_FRIENDLY_NAME = {
  [CloudType.aws]: 'AWS',
  [CloudType.gcp]: 'GCP',
  [CloudType.azu]: 'Azure'
};

const TIME_SYNC_FIELD = 'useTimeSync';

export const TimeSyncField: FC<TimeSyncProps> = ({ provider }) => {
  const { control } = useFormContext<OtherAdvancedProps>();
  const { t } = useTranslation();

  const { data } = useQuery(QUERY_KEY.getProvidersList, api.getProvidersList);
  const isChronyEnabled = !!data?.find((p) => p?.uuid === provider?.uuid)?.details?.setUpChrony;

  const stringMap = { provider: PROVIDER_FRIENDLY_NAME[provider?.code] };

  return (
    <Box
      sx={{
        display: 'flex',
        flexDirection: 'column'
      }}
    >
      <YBCheckboxField
        name={TIME_SYNC_FIELD}
        control={control}
        label={t('universeForm.instanceConfig.useTimeSync', stringMap)}
        size="large"
        disabled={isChronyEnabled}
      />
    </Box>
  );
};

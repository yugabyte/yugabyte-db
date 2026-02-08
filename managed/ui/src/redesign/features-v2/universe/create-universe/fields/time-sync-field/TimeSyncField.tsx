import { FC } from 'react';
import { toUpper } from 'lodash';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { mui, YBCheckboxField } from '@yugabyte-ui-library/core';
import { OtherAdvancedProps } from '../../steps/advanced-settings/dtos';
import { QUERY_KEY, api } from '../../../../../features/universe/universe-form/utils/api';
import { useQuery } from 'react-query';
import { ProviderType } from '../../steps/general-settings/dtos';

const { Box } = mui;

interface TimeSyncProps {
  disabled: boolean;
  provider: ProviderType;
}

const TIME_SYNC_FIELD = 'useTimeSync';

export const TimeSyncField: FC<TimeSyncProps> = ({ provider }) => {
  const { control } = useFormContext<OtherAdvancedProps>();
  const { t } = useTranslation();

  const { data } = useQuery(QUERY_KEY.getProvidersList, api.getProvidersList);
  const isChronyEnabled = !!data?.find((p) => p?.uuid === provider?.uuid)?.details?.setUpChrony;

  const stringMap = { provider: toUpper(provider?.code) };

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
        label={t('createUniverseV2.instanceSettings.useTimeSync', stringMap)}
        size="large"
        disabled={isChronyEnabled}
        dataTestId="time-sync-field"
      />
    </Box>
  );
};

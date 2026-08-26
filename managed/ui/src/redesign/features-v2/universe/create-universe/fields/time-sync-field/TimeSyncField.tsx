import { FC } from 'react';
import { toUpper } from 'lodash';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { mui, YBToggleField, YBTooltip } from '@yugabyte-ui-library/core';
import { FieldContainer } from '../../components/DefaultComponents';
import { OtherAdvancedProps } from '../../steps/advanced-settings/dtos';
import { QUERY_KEY, api } from '../../../../../features/universe/universe-form/utils/api';
import { useQuery } from 'react-query';
import { ProviderType } from '../../steps/general-settings/dtos';

const { Box, styled, Typography } = mui;

import InfoIcon from '../../../../../assets/approved/info-new.svg';

interface TimeSyncProps {
  disabled: boolean;
  provider: ProviderType;
}

const TIME_SYNC_FIELD = 'useTimeSync';

const StyledSubText = styled(Typography)({
  fontSize: '11.5px',
  lineHeight: '16px',
  fontWeight: 400,
  color: '#67666C',
  marginLeft: '8px'
});

export const TimeSyncField: FC<TimeSyncProps> = ({ provider, disabled }) => {
  const { control } = useFormContext<OtherAdvancedProps>();
  const { t } = useTranslation();

  const { data } = useQuery(QUERY_KEY.getProvidersList, api.getProvidersList);
  const isChronyEnabled = !!data?.find((p) => p?.uuid === provider?.uuid)?.details?.setUpChrony;

  const stringMap = { provider: toUpper(provider?.code) };

  return (
    <FieldContainer sx={{ padding: '16px 24px', gap: '4px' }}>
      <Box
        sx={{
          display: 'flex',
          flexDirection: 'row',
          alignItems: 'center',
          gap: '8px'
        }}
        data-testid="TimeSyncField-Container"
      >
        <div style={{ marginBottom: '-5px' }}>
          <YBToggleField
            name={TIME_SYNC_FIELD}
            inputProps={{
              'data-testid': 'TimeSync-Toggle'
            }}
            control={control}
            disabled={disabled || isChronyEnabled}
            dataTestId="time-sync-field"
            label={t('createUniverseV2.instanceSettings.useTimeSync', stringMap)}
          />
        </div>
        <YBTooltip title={t('createUniverseV2.instanceSettings.useTimeSyncTooltip')}>
          <div style={{ marginBottom: '-5px' }}>
            <InfoIcon />
          </div>
        </YBTooltip>
      </Box>
      <Box sx={{ ml: 5 }}>
        <StyledSubText>
          {t('createUniverseV2.instanceSettings.useTimeSyncHelper2', stringMap)}
        </StyledSubText>
      </Box>
    </FieldContainer>
  );
};

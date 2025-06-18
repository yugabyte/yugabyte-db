import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { mui, YBCheckboxField } from '@yugabyte-ui-library/core';
import { OtherAdvancedProps } from '../../steps/advanced-settings/dtos';

const { Box, Typography, styled } = mui;

interface TimeSyncProps {
  disabled: boolean;
}

const TIME_SYNC_FIELD = 'useTimeSync';

export const TimeSyncField: FC<TimeSyncProps> = ({ disabled }) => {
  const { control } = useFormContext<OtherAdvancedProps>();
  const { t } = useTranslation();

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
        label={'Use AWS time Sync'}
        size="large"
      />
    </Box>
  );
};

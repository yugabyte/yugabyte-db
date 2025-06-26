import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { mui, YBCheckboxField } from '@yugabyte-ui-library/core';
import { OtherAdvancedProps } from '../../steps/advanced-settings/dtos';

const { Box, Typography, styled } = mui;

interface SystemDFieldProps {
  disabled: boolean;
}

const SYSTEMD_FIELD = 'useSystemd';

export const SystemDField: FC<SystemDFieldProps> = ({ disabled }) => {
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
        name={SYSTEMD_FIELD}
        control={control}
        label={'Enable SystemD Services'}
        size="large"
      />
    </Box>
  );
};

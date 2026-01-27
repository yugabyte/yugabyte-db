import { FC } from 'react';
import { toUpper } from 'lodash';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { mui, YBLabel, YBCheckboxField } from '@yugabyte-ui-library/core';
import { CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';
import { InstanceSettingProps } from '@app/redesign/features-v2/universe/create-universe/steps/hardware-settings/dtos';
import { SPOT_INSTANCE_FIELD } from '@app/redesign/features-v2/universe/create-universe/fields/FieldNames';

const { Box, styled, Typography } = mui;

interface SpotInstanceFieldProps {
  disabled: boolean;
  cloudType: CloudType;
}

const StyledSubText = styled(Typography)(({ theme }) => ({
  fontSize: 11.5,
  lineHeight: '16px',
  fontWeight: 400,
  color: theme.palette.grey[600]
}));

export const SpotInstanceField: FC<SpotInstanceFieldProps> = ({ cloudType, disabled }) => {
  const { control } = useFormContext<InstanceSettingProps>();
  const { t } = useTranslation('translation', { keyPrefix: 'universeForm.instanceConfig' });

  return (
    <Box sx={{ display: 'flex', width: '100%', flexDirection: 'column' }}>
      <Box sx={{ display: 'flex', flexDirection: 'row', alignItems: 'center', marginBottom: 1 }}>
        <YBLabel>{t('spotInstance', { cloudType: toUpper(cloudType) })}</YBLabel>&nbsp;
        <StyledSubText>| {t('cantChangeLater')}</StyledSubText>
      </Box>
      <Box>
        <YBCheckboxField
          label={t('useAwsSpotInstance', { cloudType: toUpper(cloudType) })}
          control={control}
          name={SPOT_INSTANCE_FIELD}
          size="large"
          dataTestId="spot-instance-field"
          disabled={disabled}
        />
      </Box>
    </Box>
  );
};

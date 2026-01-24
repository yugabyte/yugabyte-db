import { FC, useEffect } from 'react';
import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { mui, YBLabel, RadioOrientation, YBRadioGroupField } from '@yugabyte-ui-library/core';
import { ArchitectureType } from '@app/redesign/features-v2/universe/create-universe/helpers/constants';
import { InstanceSettingProps } from '@app/redesign/features-v2/universe/create-universe/steps/hardware-settings/dtos';
import { CPU_ARCH_FIELD } from '@app/redesign/features-v2/universe/create-universe/fields/FieldNames';

const { Box, styled, Typography } = mui;

interface CPUArchFieldProps {
  disabled: boolean;
  supportedArchs: ArchitectureType[];
}

const StyledSubText = styled(Typography)(({ theme }) => ({
  fontSize: 11.5,
  lineHeight: '16px',
  fontWeight: 400,
  color: theme.palette.grey[600]
}));

export const CPUArchField: FC<CPUArchFieldProps> = ({ disabled, supportedArchs }) => {
  const { control, watch, setValue } = useFormContext<InstanceSettingProps>();
  const { t } = useTranslation('translation', { keyPrefix: 'universeForm.instanceConfig' });
  const fieldValue = watch(CPU_ARCH_FIELD);

  const architectures = [
    {
      value: ArchitectureType.X86_64,
      label: t(ArchitectureType.X86_64),
      disabled: !supportedArchs.includes(ArchitectureType.X86_64),
      tooltip: !supportedArchs.includes(ArchitectureType.X86_64) ? t('cpuArchNotSupported') : ''
    },
    {
      value: ArchitectureType.ARM64,
      label: t(ArchitectureType.ARM64),
      disabled: !supportedArchs.includes(ArchitectureType.ARM64),
      tooltip: !supportedArchs.includes(ArchitectureType.ARM64) ? t('cpuArchNotSupported') : ''
    }
  ];

  useEffect(() => {
    if (!supportedArchs.includes(fieldValue) && fieldValue) {
      setValue(CPU_ARCH_FIELD, supportedArchs[0]);
    }
  }, [fieldValue, supportedArchs]);

  return (
    <Box sx={{ display: 'flex', width: '100%', flexDirection: 'column' }}>
      <Box sx={{ display: 'flex', flexDirection: 'row', alignItems: 'center', marginBottom: 1 }}>
        <YBLabel>{t('cpuArch')}</YBLabel>&nbsp;
        <StyledSubText>| {t('cantChangeLater')}</StyledSubText>
      </Box>
      <YBRadioGroupField
        options={architectures}
        orientation={RadioOrientation.Horizontal}
        control={control}
        name={CPU_ARCH_FIELD}
        sx={{ display: 'flex', flexDirection: 'row', gap: 4 }}
        dataTestId="cpu-architecture-field"
        disabled={disabled}
      />
    </Box>
  );
};

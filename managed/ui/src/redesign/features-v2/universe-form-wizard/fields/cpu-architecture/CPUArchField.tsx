import { FC } from 'react';
import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { mui, YBLabel, RadioOrientation, YBRadioGroupField } from '@yugabyte-ui-library/core';
import { ArchitectureType } from '@app/redesign/features-v2/universe-form-wizard/helpers/constants';
import { InstanceSettingProps } from '@app/redesign/features-v2/universe-form-wizard/steps/hardware-settings/dtos';
import { CPU_ARCH_FIELD } from '@app/redesign/features-v2/universe-form-wizard/fields/FieldNames';

const { Box, styled, Typography } = mui;

interface CPUArchFieldProps {
  disabled: boolean;
}

const StyledSubText = styled(Typography)(({ theme }) => ({
  fontSize: 11.5,
  lineHeight: '16px',
  fontWeight: 400,
  color: theme.palette.grey[600]
}));

export const CPUArchField: FC<CPUArchFieldProps> = ({ disabled }) => {
  const { control } = useFormContext<InstanceSettingProps>();
  const { t } = useTranslation('translation', { keyPrefix: 'universeForm.instanceConfig' });

  const IntelArch = {
    value: ArchitectureType.X86_64,
    label: t(ArchitectureType.X86_64)
  };
  const ArmArch = {
    value: ArchitectureType.ARM64,
    label: t(ArchitectureType.ARM64)
  };

  const supportedArch = [IntelArch, ArmArch];

  return (
    <Box sx={{ display: 'flex', width: '100%', flexDirection: 'column' }}>
      <Box sx={{ display: 'flex', flexDirection: 'row', alignItems: 'center', marginBottom: 1 }}>
        <YBLabel>{t('cpuArch')}</YBLabel>&nbsp;
        <StyledSubText>| {t('cantChangeLater')}</StyledSubText>
      </Box>
      <YBRadioGroupField
        options={supportedArch}
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

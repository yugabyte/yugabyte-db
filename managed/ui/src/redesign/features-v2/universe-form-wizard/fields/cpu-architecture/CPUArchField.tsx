/* eslint-disable react/display-name */
/*
 * Created on Mon Nov 13 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { useFormContext } from 'react-hook-form';
import { mui, YBLabel, RadioOrientation, YBRadioGroupField } from '@yugabyte-ui-library/core';
import { useTranslation } from 'react-i18next';
import { ArchitectureType } from '../../helpers/constants';
import { InstanceSettingProps } from '../../steps/hardware-settings/dtos';

const { Box, styled, Typography } = mui;

interface CPUArchFieldProps {
  disabled: boolean;
}

const CPU_ARCH_FIELD = 'arch';

const StyledSubText = styled(Typography)(({ theme }) => ({
  fontSize: 11.5,
  lineHeight: '16px',
  fontWeight: 400,
  color: '#6D7C88'
}));

export const CPUArchField: FC<CPUArchFieldProps> = ({ disabled }) => {
  const { setValue, control } = useFormContext<InstanceSettingProps>();
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
    <Box sx={{ display: 'flex', width: '100%', flexDirection: 'column', gap: '12px' }}>
      <Box sx={{ display: 'flex', flexDirection: 'row', gap: '4px', alignItems: 'center' }}>
        <YBLabel>{t('cpuArch')}</YBLabel>
        <StyledSubText>| This CANNOT be changed after creation</StyledSubText>
      </Box>
      <YBRadioGroupField
        options={supportedArch}
        orientation={RadioOrientation.Horizontal}
        control={control}
        name={CPU_ARCH_FIELD}
      />
    </Box>
  );
};

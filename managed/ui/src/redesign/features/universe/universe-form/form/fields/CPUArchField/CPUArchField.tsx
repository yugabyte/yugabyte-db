/* eslint-disable react/display-name */
/*
 * Created on Mon Nov 13 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useEffect } from 'react';
import { useController, useFormContext, useWatch } from 'react-hook-form';
import { CloudType, UniverseFormData } from '../../../utils/dto';
import { Box } from '@material-ui/core';
import { YBButtonGroup, YBLabel } from '../../../../../../components';
import { useFormFieldStyles } from '../../../universeMainStyle';
import { useTranslation } from 'react-i18next';
import { CPU_ARCHITECTURE_FIELD, PROVIDER_FIELD } from '../../../utils/constants';
import { ArchitectureType } from '../../../../../../../components/configRedesign/providerRedesign/constants';

interface CPUArchFieldProps {
  disabled: boolean;
}

export const CPUArchField: FC<CPUArchFieldProps> = ({ disabled }) => {
  const { setValue } = useFormContext<UniverseFormData>();
  const { t } = useTranslation('translation', { keyPrefix: 'universeForm.instanceConfig' });

  const provider = useWatch({ name: PROVIDER_FIELD });

  const {
    field: { value }
  } = useController({
    name: CPU_ARCHITECTURE_FIELD
  });

  const fieldClasses = useFormFieldStyles();

  useEffect(() => {
    if (value === null) {
      setValue(CPU_ARCHITECTURE_FIELD, ArchitectureType.X86_64, { shouldValidate: true });
    }
  }, [value]);

  const IntelArch = {
    value: ArchitectureType.X86_64,
    label: t(ArchitectureType.X86_64)
  };
  const ArmArch = {
    value: ArchitectureType.ARM64,
    label: t(ArchitectureType.ARM64)
  };

  const supportedArch = [IntelArch, ArmArch];

  if (provider?.code !== CloudType.aws) return null;

  const handleSelect = (option: typeof supportedArch[number]) => {
    setValue(CPU_ARCHITECTURE_FIELD, option.value, {
      shouldValidate: true
    });
  };

  const getLabel = (val: ArchitectureType) => {
    return val === ArchitectureType.X86_64 ? t(ArchitectureType.X86_64) : t(ArchitectureType.ARM64);
  };

  return (
    <Box width="100%" display="flex" data-testid="cpuArchitecture-Container">
      <YBLabel dataTestId="ReplicationFactor-Label">{t('cpuArch')}</YBLabel>
      <Box flex={1} className={fieldClasses.defaultTextBox}>
        <YBButtonGroup
          dataTestId={'cpuArchitecture'}
          variant={'contained'}
          color={'default'}
          values={supportedArch}
          selectedNum={{ label: getLabel(value), value }}
          displayLabelFn={(t) => <>{t.label}</>}
          disabled={disabled}
          handleSelect={handleSelect}
        />
      </Box>
    </Box>
  );
};

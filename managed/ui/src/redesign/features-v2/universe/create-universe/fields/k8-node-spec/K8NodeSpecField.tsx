import { ReactElement, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { Controller, useFormContext } from 'react-hook-form';
import { YBLabel, YBInput, mui } from '@yugabyte-ui-library/core';
import {
  getDefaultK8NodeSpec,
  getK8MemorySizeRange,
  getK8CPUCoresRange
} from '@app/redesign/features-v2/universe/create-universe/fields/k8-node-spec/K8NodeSpecFieldHelper';
import { useRuntimeConfigValues } from '@app/redesign/features-v2/universe/create-universe/helpers/utils';
import { NodeType } from '@app/redesign/utils/dtos';
import { ProviderType } from '@app/redesign/features-v2/universe/create-universe/steps/general-settings/dtos';
import { InstanceSettingProps } from '@app/redesign/features-v2/universe/create-universe/steps/hardware-settings/dtos';
import {
  MASTER_K8_NODE_SPEC_FIELD,
  TSERVER_K8_NODE_SPEC_FIELD
} from '@app/redesign/features-v2/universe/create-universe/fields/FieldNames';

const { Box } = mui;

interface K8NodeSpecFieldProps {
  isMaster: boolean;
  disabled: boolean;
  provider?: ProviderType;
}

export const K8NodeSpecField = ({
  isMaster,
  disabled,
  provider
}: K8NodeSpecFieldProps): ReactElement => {
  const { watch, control, setValue } = useFormContext<InstanceSettingProps>();
  const { t } = useTranslation('translation', { keyPrefix: 'universeForm.instanceConfig' });

  const nodeTypeTag = isMaster ? NodeType.Master : NodeType.TServer;
  const fieldValue = isMaster
    ? watch(MASTER_K8_NODE_SPEC_FIELD)
    : watch(TSERVER_K8_NODE_SPEC_FIELD);
  const UPDATE_FIELD = isMaster ? MASTER_K8_NODE_SPEC_FIELD : TSERVER_K8_NODE_SPEC_FIELD;
  const convertToString = (str: string | number) => str?.toString() ?? '';

  //fetch run time configs
  const { providerRuntimeConfigs } = useRuntimeConfigValues(provider?.uuid);

  //update memory and cpu from provider runtime configs
  useEffect(() => {
    const updateDeviceInfo = () => {
      const { memorySize, CPUCores } = getDefaultK8NodeSpec(providerRuntimeConfigs);
      const nodeSpec = {
        memoryGib: memorySize,
        cpuCoreCount: CPUCores
      };
      setValue(UPDATE_FIELD, nodeSpec);
    };
    !fieldValue && updateDeviceInfo();
  }, [fieldValue]);

  const { minMemorySize, maxMemorySize } = getK8MemorySizeRange(providerRuntimeConfigs);
  const { minCPUCores, maxCPUCores } = getK8CPUCoresRange(providerRuntimeConfigs);
  const onNumCoresChanged = (value: any) => {
    const decimalPaces = value?.split?.('.')[1]?.length ?? 0;
    const numCores = decimalPaces > 2 ? Number(Number(value).toFixed(2)) : Number(value);
    setValue(UPDATE_FIELD, {
      memoryGib: fieldValue?.memoryGib as number,
      cpuCoreCount:
        numCores > maxCPUCores ? maxCPUCores : numCores < minCPUCores ? minCPUCores : numCores
    });
  };

  const onMemoryChanged = (value: any) => {
    const decimalPaces = value?.split?.('.')[1]?.length ?? 0;
    const memory = decimalPaces > 2 ? Number(value).toFixed(2) : Number(value);
    setValue(UPDATE_FIELD, {
      cpuCoreCount: fieldValue?.cpuCoreCount as number,
      memoryGib:
        memory > maxMemorySize ? maxMemorySize : memory < minMemorySize ? minMemorySize : memory
    });
  };

  return (
    <Controller
      name={UPDATE_FIELD}
      control={control}
      rules={{
        required: t('universeForm.validation.required', {
          field: t('universeForm.instanceConfig.instanceType')
        }) as string
      }}
      render={() => {
        return (
          <Box display="flex" width="100%" flexDirection="column">
            <Box display="flex" sx={{ width: 198 }}>
              <Box flex={1}>
                <YBInput
                  type="number"
                  label={t('k8NumCores')}
                  fullWidth
                  slotProps={{
                    htmlInput: {
                      'data-testid': `K8NodeSpecField-${nodeTypeTag}-NumCoresInput`
                    }
                  }}
                  value={convertToString(fieldValue?.cpuCoreCount ?? '')}
                  disabled={disabled}
                  onChange={(event) => onNumCoresChanged(event.target.value)}
                  inputMode="numeric"
                  dataTestId={`K8NodeSpecField-${nodeTypeTag}-NumCoresInput`}
                />
              </Box>
            </Box>

            <Box display="flex" flexDirection="column" mt={2}>
              <Box display="flex">
                <Box>
                  <YBLabel>{t('memory')}</YBLabel>
                </Box>
              </Box>
              <Box display="flex" width="100%">
                <Box display="flex" sx={{ width: 198 }}>
                  <YBInput
                    type="number"
                    fullWidth
                    slotProps={{
                      htmlInput: {
                        'data-testid': `K8NodeSpecField-${nodeTypeTag}-MemoryInput`
                      }
                    }}
                    value={convertToString(fieldValue?.memoryGib ?? '')}
                    disabled={disabled}
                    onChange={(event) => onMemoryChanged(event.target.value)}
                    inputMode="numeric"
                    dataTestId={`K8NodeSpecField-${nodeTypeTag}-MemoryInput`}
                  />
                </Box>
                <Box
                  ml={2}
                  display="flex"
                  alignItems="center"
                  sx={(theme) => ({
                    marginLeft: theme.spacing(2),
                    alignSelf: 'flex-end',
                    marginBottom: 1
                  })}
                >
                  {t('k8VolumeSizeUnit')}
                </Box>
              </Box>
            </Box>
          </Box>
        );
      }}
    />
  );
};

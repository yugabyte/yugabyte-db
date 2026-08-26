import { ReactElement, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { Controller, useFormContext } from 'react-hook-form';
import { YBLabel, YBInput, mui } from '@yugabyte-ui-library/core';
import { getDefaultK8NodeSpec } from '@app/redesign/features-v2/universe/create-universe/fields/k8-node-spec/K8NodeSpecFieldHelper';
import { useRuntimeConfigValues } from '@app/redesign/features-v2/universe/create-universe/helpers/utils';
import { NodeType } from '@app/redesign/utils/dtos';
import { ProviderType } from '@app/redesign/features-v2/universe/create-universe/steps/general-settings/dtos';
import { InstanceSettingProps } from '@app/redesign/features-v2/universe/create-universe/steps/hardware-settings/dtos';
import {
  MASTER_K8_NODE_SPEC_FIELD,
  TSERVER_K8_NODE_SPEC_FIELD
} from '@app/redesign/features-v2/universe/create-universe/fields/FieldNames';
import {
  parsePositiveDecimalInput,
  sanitizePositiveDecimalString
} from '@app/redesign/features-v2/universe/create-universe/helpers/instanceNumericInput';

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
  const {
    watch,
    control,
    setValue,
    formState: { errors, isSubmitted }
  } = useFormContext<InstanceSettingProps>();
  const { t } = useTranslation();

  const nodeTypeTag = isMaster ? NodeType.Master : NodeType.TServer;
  const fieldValue = isMaster
    ? watch(MASTER_K8_NODE_SPEC_FIELD)
    : watch(TSERVER_K8_NODE_SPEC_FIELD);
  const UPDATE_FIELD = isMaster ? MASTER_K8_NODE_SPEC_FIELD : TSERVER_K8_NODE_SPEC_FIELD;
  const convertToString = (str: string | number) => str?.toString() ?? '';
  const k8FieldErrors = (isMaster
    ? errors.masterK8SNodeResourceSpec
    : errors.tserverK8SNodeResourceSpec) as
    | {
        memoryGib?: { message?: string };
        cpuCoreCount?: { message?: string };
      }
    | undefined;

  const setNodeSpec = (next: { memoryGib: number | null; cpuCoreCount: number | null }) => {
    setValue(UPDATE_FIELD, next, { shouldValidate: isSubmitted, shouldDirty: true });
  };

  //fetch run time configs
  const { providerRuntimeConfigs } = useRuntimeConfigValues(provider?.uuid);

  //update memory and cpu from provider runtime configs
  useEffect(() => {
    const updateDeviceInfo = () => {
      const { memorySize, CPUCores } = getDefaultK8NodeSpec(providerRuntimeConfigs);
      const resolvedMemory = Number(memorySize);
      const resolvedCpu = Number(CPUCores);
      const nodeSpec = {
        memoryGib: Number.isFinite(resolvedMemory) ? resolvedMemory : null,
        cpuCoreCount: Number.isFinite(resolvedCpu) ? resolvedCpu : null
      };
      setValue(UPDATE_FIELD, nodeSpec);
    };
    !fieldValue && updateDeviceInfo();
  }, [fieldValue]);

  const onNumCoresChanged = (value: any) => {
    const raw = sanitizePositiveDecimalString(String(value));
    const decimalPlaces = raw.split('.')[1]?.length ?? 0;
    let numCores = parsePositiveDecimalInput(raw);
    if (numCores != null && decimalPlaces > 2) {
      numCores = Number(numCores.toFixed(2));
    }
    setNodeSpec({
      memoryGib: fieldValue?.memoryGib ?? null,
      cpuCoreCount: numCores
    });
  };

  const onMemoryChanged = (value: any) => {
    const raw = sanitizePositiveDecimalString(String(value));
    const decimalPlaces = raw.split('.')[1]?.length ?? 0;
    let memory = parsePositiveDecimalInput(raw);
    if (memory != null && decimalPlaces > 2) {
      memory = Number(Number(memory).toFixed(2));
    }
    setNodeSpec({
      cpuCoreCount: fieldValue?.cpuCoreCount ?? null,
      memoryGib: memory
    });
  };

  return (
    <Controller
      name={UPDATE_FIELD}
      control={control}
      rules={{
        required: t('createUniverseV2.instanceSettings.validation.required', {
          field: t('createUniverseV2.instanceSettings.instanceType')
        }) as string
      }}
      render={() => {
        return (
          <Box display="flex" width="100%" flexDirection="column">
            <Box display="flex" sx={{ width: 198 }}>
              <Box flex={1}>
                <YBInput
                  type="number"
                  label={t('createUniverseV2.instanceSettings.k8NumCores')}
                  fullWidth
                  error={!!k8FieldErrors?.cpuCoreCount}
                  helperText={k8FieldErrors?.cpuCoreCount?.message}
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
                  <YBLabel error={!!k8FieldErrors?.memoryGib}>
                    {t('createUniverseV2.instanceSettings.k8Memory')}
                  </YBLabel>
                </Box>
              </Box>
              <Box display="flex" width="100%" alignItems="flex-start">
                <Box display="flex" sx={{ width: 198 }}>
                  <YBInput
                    type="number"
                    fullWidth
                    error={!!k8FieldErrors?.memoryGib}
                    helperText={k8FieldErrors?.memoryGib?.message}
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
                    height: 40,
                    flexShrink: 0
                  })}
                >
                  {t('createUniverseV2.instanceSettings.k8VolumeSizeUnit')}
                </Box>
              </Box>
            </Box>
          </Box>
        );
      }}
    />
  );
};

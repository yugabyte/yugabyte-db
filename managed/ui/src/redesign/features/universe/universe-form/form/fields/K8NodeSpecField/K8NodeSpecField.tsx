import { ReactElement, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { Controller, useFormContext, useWatch } from 'react-hook-form';
import { useQuery } from 'react-query';
import { Box, Grid } from '@material-ui/core';
import { YBLabel, YBInput } from '../../../../../../components';
import { api, QUERY_KEY } from '../../../utils/api';
import {
  getDefaultK8NodeSpec,
  getK8MemorySizeRange,
  getK8CPUCoresRange
} from './K8NodeSpecFieldHelper';
import { UniverseFormData } from '../../../utils/dto';
import { NodeType } from '../../../../../../utils/dtos';
import {
  MASTER_K8_NODE_SPEC_FIELD,
  PROVIDER_FIELD,
  TSERVER_K8_NODE_SPEC_FIELD
} from '../../../utils/constants';

interface K8NodeSpecFieldProps {
  isDedicatedMasterField: boolean;
  isEditMode: boolean;
}

export const K8NodeSpecField = ({
  isDedicatedMasterField,
  isEditMode
}: K8NodeSpecFieldProps): ReactElement => {
  const { control, setValue } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();
  const nodeTypeTag = isDedicatedMasterField ? NodeType.Master : NodeType.TServer;

  //watchers
  const provider = useWatch({ name: PROVIDER_FIELD });
  const fieldValue = isDedicatedMasterField
    ? useWatch({ name: MASTER_K8_NODE_SPEC_FIELD })
    : useWatch({ name: TSERVER_K8_NODE_SPEC_FIELD });
  const UPDATE_FIELD = isDedicatedMasterField
    ? MASTER_K8_NODE_SPEC_FIELD
    : TSERVER_K8_NODE_SPEC_FIELD;
  const convertToString = (str: string) => str?.toString() ?? '';

  //fetch run time configs
  const { data: providerRuntimeConfigs, refetch: providerConfigsRefetch } = useQuery(
    QUERY_KEY.fetchProviderRunTimeConfigs,
    () => api.fetchRunTimeConfigs(true, provider?.uuid)
  );

  //update memory and cpu from provider runtime configs
  useEffect(() => {
    const getProviderRuntimeConfigs = async () => {
      await providerConfigsRefetch();
      const { memorySize, CPUCores } = getDefaultK8NodeSpec(providerRuntimeConfigs);
      let nodeSpec = {
        memoryGib: memorySize,
        cpuCoreCount: CPUCores
      };

      if (fieldValue && nodeSpec && isEditMode) {
        nodeSpec.memoryGib = fieldValue.memoryGib;
        nodeSpec.cpuCoreCount = fieldValue.cpuCoreCount;
      }
      setValue(UPDATE_FIELD, nodeSpec);
    };
    getProviderRuntimeConfigs();
  }, []);

  const { minMemorySize, maxMemorySize } = getK8MemorySizeRange(providerRuntimeConfigs);
  const { minCPUCores, maxCPUCores } = getK8CPUCoresRange(providerRuntimeConfigs);
  const onNumCoresChanged = (value: any) => {
    const decimalPaces = value?.split?.('.')[1]?.length ?? 0;
    const numCores = decimalPaces > 2 ? Number(value).toFixed(2) : Number(value);
    setValue(UPDATE_FIELD, {
      ...fieldValue,
      cpuCoreCount:
        numCores > maxCPUCores ? maxCPUCores : numCores < minCPUCores ? minCPUCores : numCores
    });
  };

  const onMemoryChanged = (value: any) => {
    const decimalPaces = value?.split?.('.')[1]?.length ?? 0;
    const memory = decimalPaces > 2 ? Number(value).toFixed(2) : Number(value);
    setValue(UPDATE_FIELD, {
      ...fieldValue,
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
      render={({}) => {
        return (
          <Box display="flex" width="100%" flexDirection="column" mt={2}>
            <Grid container spacing={2}>
              <Grid item lg={6} sm={12}>
                <Box display="flex" mt={1}>
                  <YBLabel dataTestId={`K8NodeSpecField-${nodeTypeTag}-NumCoresLabel`}>
                    {t('universeForm.instanceConfig.k8NumCores')}
                  </YBLabel>
                  <Box flex={1}>
                    <YBInput
                      type="number"
                      fullWidth
                      inputProps={{
                        'data-testid': `K8NodeSpecField-${nodeTypeTag}-NumCoresInput`
                      }}
                      value={convertToString(fieldValue?.cpuCoreCount)}
                      onChange={(event) => onNumCoresChanged(event.target.value)}
                      inputMode="numeric"
                    />
                  </Box>
                </Box>
              </Grid>
            </Grid>

            <Grid container spacing={2}>
              <Grid item lg={6} sm={12}>
                <Box display="flex" mt={1}>
                  <YBLabel dataTestId={`K8NodeSpecField-${nodeTypeTag}-MemoryLabel`}>
                    {t('universeForm.instanceConfig.k8Memory')}
                  </YBLabel>
                  <Box flex={1}>
                    <YBInput
                      type="number"
                      fullWidth
                      inputProps={{
                        'data-testid': `K8NodeSpecField-${nodeTypeTag}-MemoryInput`
                      }}
                      value={convertToString(fieldValue?.memoryGib)}
                      onChange={(event) => onMemoryChanged(event.target.value)}
                      inputMode="numeric"
                    />
                  </Box>
                </Box>
              </Grid>
            </Grid>
          </Box>
        );
      }}
    />
  );
};

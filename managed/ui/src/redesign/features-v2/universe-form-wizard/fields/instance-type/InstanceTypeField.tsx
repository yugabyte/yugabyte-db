import { ChangeEvent, ReactElement, useContext } from 'react';
import pluralize from 'pluralize';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { useUpdateEffect } from 'react-use';
import { Controller, useFormContext } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBAutoComplete } from '@yugabyte-ui-library/core';
import { QUERY_KEY, api } from '@app/redesign/features/universe/universe-form/utils/api';
import { sortAndGroup, getDefaultInstanceType, useGetAllZones } from './InstanceTypeFieldHelper';
import { NodeType } from '@app/redesign/utils/dtos';
import { IsOsPatchingEnabled } from '@app/components/configRedesign/providerRedesign/components/linuxVersionCatalog/LinuxVersionUtils';
import {
  CloudType,
  InstanceType,
  InstanceTypeWithGroup,
  Placement
} from '@app/redesign/features/universe/universe-form/utils/dto';
import { CreateUniverseContext, CreateUniverseContextMethods } from '../../CreateUniverseContext';
import { InstanceSettingProps } from '../../steps/hardware-settings/dtos';
import { INSTANCE_TYPE_FIELD, MASTER_INSTANCE_TYPE_FIELD } from '../FieldNames';

const getOptionLabel = (op: Record<string, string> | string): string => {
  if (!op) return '';

  const option = (op as unknown) as InstanceType;
  let result = option.instanceTypeCode;
  if (option.numCores && option.memSizeGB) {
    const cores = pluralize('core', option.numCores, true);
    result = `${option.instanceTypeCode} (${cores}, ${option.memSizeGB}GB RAM)`;
  }
  return result;
};

const renderOption = (
  props: React.HTMLAttributes<HTMLLIElement>,
  option: Record<string, string>
): React.ReactNode => {
  return <li {...props}>{getOptionLabel(option)}</li>;
};

interface InstanceTypeFieldProps {
  isEditMode?: boolean;
  isMaster?: boolean;
  disabled: boolean;
}

export const InstanceTypeField = ({ isMaster, disabled }: InstanceTypeFieldProps): ReactElement => {
  const { control, setValue, getValues, watch } = useFormContext<InstanceSettingProps>();
  const { t } = useTranslation();
  const nodeTypeTag = isMaster ? NodeType.Master : NodeType.TServer;

  // To set value based on master or tserver field in dedicated mode
  const UPDATE_FIELD = isMaster ? MASTER_INSTANCE_TYPE_FIELD : INSTANCE_TYPE_FIELD;

  const [{ generalSettings, resilienceAndRegionsSettings }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;

  const cpuArch = watch('arch');
  const provider = generalSettings?.providerConfiguration;

  const zones = useGetAllZones(provider, resilienceAndRegionsSettings?.regions).map(
    (zone: Placement) => zone.name
  );

  const handleChange = (e: ChangeEvent<{}>, option: any) => {
    setValue(UPDATE_FIELD, option?.instanceTypeCode, { shouldValidate: true });
  };

  //fetch run time configs
  const {
    data: providerRuntimeConfigs,
    refetch: providerConfigsRefetch
  } = useQuery(QUERY_KEY.fetchProviderRunTimeConfigs, () =>
    api.fetchRunTimeConfigs(true, provider?.uuid)
  );

  const isOsPatchingEnabled = IsOsPatchingEnabled();

  const { data, isLoading, refetch } = useQuery(
    [
      QUERY_KEY.getInstanceTypes,
      provider?.uuid,
      JSON.stringify(zones),
      isOsPatchingEnabled ? cpuArch : null
    ],
    () => api.getInstanceTypes(provider?.uuid, zones, isOsPatchingEnabled ? cpuArch : null),
    {
      enabled: !!provider?.uuid && zones.length > 0,
      onSuccess: (data) => {
        if (!data.length) return;

        const currentInstanceType = getValues(UPDATE_FIELD);

        //do instance type exists on the last fetched list
        const instanceExists = (instanceType: string) =>
          !!data.find(
            (instance: InstanceType) => instance.instanceTypeCode === instanceType // default instance type exists in the list
          );

        // set default/first item as instance type after provider changes
        if (provider && (!currentInstanceType || !instanceExists(currentInstanceType))) {
          const defaultInstanceType = getDefaultInstanceType(provider.code, providerRuntimeConfigs);
          if (instanceExists(defaultInstanceType))
            setValue(UPDATE_FIELD, defaultInstanceType, { shouldValidate: true });
          else setValue(UPDATE_FIELD, data[0].instanceTypeCode, { shouldValidate: true });
        }
      }
    }
  );

  useUpdateEffect(() => {
    const getProviderRuntimeConfigs = async () => {
      await providerConfigsRefetch();
      //Reset instance type after provider change
      setValue(UPDATE_FIELD, null);
      //refetch instances based on changed provider
      refetch();
    };
    getProviderRuntimeConfigs();
  }, [provider?.uuid]);

  const instanceTypes = sortAndGroup(data, provider?.code);

  return (
    <Controller
      name={UPDATE_FIELD}
      control={control}
      rules={{
        required: t('universeForm.validation.required', {
          field: t('universeForm.instanceConfig.instanceType')
        }) as string
      }}
      render={({ field, fieldState }) => {
        const value =
          instanceTypes.find((i: InstanceTypeWithGroup) => i.instanceTypeCode === field.value) ??
          '';

        return (
          <Box
            display="flex"
            width="100%"
            data-testid={`InstanceTypeField-${nodeTypeTag}-Container`}
          >
            <Box flex={1}>
              <YBAutoComplete
                loading={isLoading}
                disabled={disabled}
                value={(value as unknown) as Record<string, string>}
                options={(instanceTypes as unknown) as Record<string, string>[]}
                getOptionLabel={getOptionLabel}
                renderOption={renderOption}
                onChange={handleChange}
                ybInputProps={{
                  error: !!fieldState.error,
                  helperText: fieldState.error?.message,
                  label: t('universeForm.instanceConfig.instanceType'),
                  dataTestId: 'instance-type-field'
                }}
                dataTestId="instance-type-field-container"
                groupBy={
                  provider?.code &&
                  [CloudType.aws, CloudType.gcp, CloudType.azu].includes(provider?.code)
                    ? (option: Record<string, string>) => option.groupName
                    : undefined
                }
              />
            </Box>
          </Box>
        );
      }}
    />
  );
};

import _ from 'lodash';
import React, { FC, useContext } from 'react';
import { useQuery } from 'react-query';
import { Controller, useFormContext } from 'react-hook-form';
import pluralize from 'pluralize';
import { Select } from '../../../../uikit/Select/Select';
import { ErrorMessage } from '../../../../uikit/ErrorMessage/ErrorMessage';
import { api, QUERY_KEY } from '../../../../helpers/api';
import { CloudType, InstanceType } from '../../../../helpers/dtos';
import { InstanceConfigFormValue } from '../../steps/instance/InstanceConfig';
import { WizardContext } from '../../UniverseWizard';
import { ControllerRenderProps } from '../../../../helpers/types';
import './InstanceTypeField.scss';

const getOptionLabel = (option: InstanceType): string => {
  let result = option.instanceTypeCode;
  if (option.numCores && option.memSizeGB) {
    const cores = pluralize('core', option.numCores, true);
    result = `${option.instanceTypeCode} (${cores}, ${option.memSizeGB}GB RAM)`;
  }
  return result;
};
const getOptionValue = (option: InstanceType): string => option.instanceTypeCode;

const DEFAULT_INSTANCE_TYPES = {
  [CloudType.aws]: 'c5.large',
  [CloudType.gcp]: 'n1-standard-1',
  [CloudType.kubernetes]: 'small'
};

const ERROR_NO_INSTANCE_TYPE = 'Instance Type value is required';

const FIELD_NAME = 'instanceType';

export const InstanceTypeField: FC = () => {
  const { control, errors, getValues, setValue } = useFormContext<InstanceConfigFormValue>();
  const { formData } = useContext(WizardContext);
  const { data } = useQuery(
    [QUERY_KEY.getInstanceTypes, formData.cloudConfig.provider?.uuid],
    api.getInstanceTypes,
    {
      enabled: !!formData.cloudConfig.provider?.uuid,
      onSuccess: (data) => {
        // preselect default instance or pick first item from the instance types list
        if (!getValues(FIELD_NAME) && formData.cloudConfig.provider?.code && data.length) {
          const defaultInstanceType =
            DEFAULT_INSTANCE_TYPES[formData.cloudConfig.provider.code] || data[0].instanceTypeCode;
          setValue(FIELD_NAME, defaultInstanceType); // intentionally omit validation as field wasn't changed by user
        }
      }
    }
  );
  const instanceTypes = _.isEmpty(data) ? [] : _.sortBy(data, 'instanceTypeCode');

  return (
    <div className="instance-type-field">
      <Controller
        control={control}
        name={FIELD_NAME}
        rules={{ required: ERROR_NO_INSTANCE_TYPE }}
        render={({ onChange, onBlur, value }: ControllerRenderProps<string | null>) => (
          <Select<InstanceType>
            isSearchable
            isClearable={false}
            className={errors[FIELD_NAME]?.message ? 'validation-error' : ''}
            getOptionLabel={getOptionLabel}
            getOptionValue={getOptionValue}
            value={instanceTypes.find((item) => item.instanceTypeCode === value) || null}
            onBlur={onBlur}
            onChange={(item) => {
              onChange((item as InstanceType).instanceTypeCode);
            }}
            options={instanceTypes}
          />
        )}
      />
      <ErrorMessage message={errors[FIELD_NAME]?.message} />
    </div>
  );
};

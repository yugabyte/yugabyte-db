import React, { FC } from 'react';
import { Controller, useFormContext } from 'react-hook-form';
import { KeyValueInput } from '../../../../uikit/KeyValueInput/KeyValueInput';
import { InstanceConfigFormValue } from '../../steps/instance/InstanceConfig';
import { translate } from '../../../../uikit/I18n/I18n';

export const InstanceTagsField: FC = () => {
  const { control } = useFormContext<InstanceConfigFormValue>();

  return (
    <Controller
      control={control}
      name="instanceTags"
      render={({ field }) => (
        <KeyValueInput
          placeholderKey={translate('Tag')}
          placeholderValue={translate('Value')}
          value={field.value}
          onChange={field.onChange}
          disabled={false}
        />
      )}
    />
  );
};

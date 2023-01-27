import React, { FC } from 'react';
import { Controller, useFormContext } from 'react-hook-form';
import { KeyValueInput } from '../../../../uikit/KeyValueInput/KeyValueInput';
import { DBConfigFormValue } from '../../steps/db/DBConfig';
import { I18n, translate } from '../../../../uikit/I18n/I18n';

interface MasterFlagsFieldProps {
  disabled: boolean;
}

export const MasterFlagsField: FC<MasterFlagsFieldProps> = ({ disabled }) => {
  const { control } = useFormContext<DBConfigFormValue>();

  return (
    <div className="master-flags-field">
      <div className="master-flags-field__head">
        <I18n className="master-flags-field__title">YB-Master</I18n>
        <div className="master-flags-field__line" />
      </div>
      <Controller
        control={control}
        name="masterGFlags"
        render={({ field }) => (
          <KeyValueInput
            placeholderKey={translate('Flag')}
            placeholderValue={translate('Value')}
            value={field.value}
            onChange={field.onChange}
            disabled={disabled}
          />
        )}
      />
    </div>
  );
};

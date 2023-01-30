import React, { FC } from 'react';
import { Controller, useFormContext } from 'react-hook-form';
import { DBConfigFormValue } from '../../steps/db/DBConfig';

import { CommunicationPortsEditor } from './CommunicationPortsEditor';
import './CommunicationPortsField.scss';

interface CommunicationPortsFieldProps {
  disabled: boolean;
}

export const CommunicationPortsField: FC<CommunicationPortsFieldProps> = ({ disabled }) => {
  const { control } = useFormContext<DBConfigFormValue>();

  return (
    <div className="communication-ports-field">
      <Controller
        control={control}
        name="communicationPorts"
        render={({ field }) => (
          <CommunicationPortsEditor
            value={field.value}
            onChange={field.onChange}
            disabled={disabled}
          />
        )}
      />
    </div>
  );
};

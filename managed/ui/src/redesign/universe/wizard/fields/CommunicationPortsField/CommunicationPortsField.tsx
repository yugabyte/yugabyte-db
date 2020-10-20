import React, { FC } from 'react';
import { Controller, useFormContext } from 'react-hook-form';
import { DBConfigFormValue } from '../../steps/db/DBConfig';
import { KeyValueInput } from '../../../../uikit/KeyValueInput/KeyValueInput';
import { ControllerRenderProps } from '../../../../helpers/types';
import { FlagsObject } from '../../../../helpers/dtos';
import './CommunicationPortsField.scss';

interface CommunicationPortsFieldProps {
  disabled: boolean;
}

const keysToLabels = {
  masterHttpPort: 'Master HTTP Port',
  masterRpcPort: 'Master RPC Port',
  tserverHttpPort: 'T-Server HTTP Port',
  tserverRpcPort: 'T-Server RPC Port',
  redisServerHttpPort: 'Yedis HTTP Port',
  redisServerRpcPort: 'Yedis RPC Port',
  yqlServerHttpPort: 'YCQL HTTP Port',
  yqlServerRpcPort: 'YCQL RPC Port',
  ysqlServerHttpPort: 'YSQL HTTP Port',
  ysqlServerRpcPort: 'YSQL RPC Port',
  nodeExporterPort: 'Node Exporter Port'
};

export const CommunicationPortsField: FC<CommunicationPortsFieldProps> = ({ disabled }) => {
  const { control } = useFormContext<DBConfigFormValue>();

  return (
    <div className="communication-ports-field">
      <Controller
        control={control}
        name="communicationPorts"
        render={({ value, onChange }: ControllerRenderProps<FlagsObject>) => (
          // TODO: KeyValueInput is temporary, redo to finalized ui/ux once it ready
          <KeyValueInput
            value={value}
            onChange={onChange}
            disabled={disabled}
            softReadonly={true}
          />
        )}
      />

    </div>
  );
};

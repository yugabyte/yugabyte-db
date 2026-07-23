import { DEFAULT_COMMUNICATION_PORTS } from './constants';
import { DatabaseSettingsProps } from '../steps/database-settings/dtos';
import { OtherAdvancedProps } from '../steps/advanced-settings/dtos';

export type ConnectionPoolingPortFields = {
  ysqlServerRpcPort: number;
  internalYsqlServerRpcPort: number;
};

export const DEFAULT_CONNECTION_POOLING_PORTS: ConnectionPoolingPortFields = {
  ysqlServerRpcPort: DEFAULT_COMMUNICATION_PORTS.ysqlServerRpcPort,
  internalYsqlServerRpcPort: DEFAULT_COMMUNICATION_PORTS.internalYsqlServerRpcPort!
};

/** Copy CP ports into Advanced deployment ports (caller must gate on CP + override). */
export function applyConnectionPoolingPortsToAdvanced(
  otherAdvancedSettings: OtherAdvancedProps | undefined,
  ports: Partial<ConnectionPoolingPortFields>
): OtherAdvancedProps | undefined {
  if (!otherAdvancedSettings) return otherAdvancedSettings;

  return {
    ...otherAdvancedSettings,
    ...(ports.ysqlServerRpcPort !== undefined && {
      ysqlServerRpcPort: ports.ysqlServerRpcPort
    }),
    ...(ports.internalYsqlServerRpcPort !== undefined && {
      internalYsqlServerRpcPort: ports.internalYsqlServerRpcPort
    })
  };
}

export function applyConnectionPoolingPortsToDatabase(
  databaseSettings: DatabaseSettingsProps | undefined,
  ports: ConnectionPoolingPortFields
): DatabaseSettingsProps | undefined {
  if (!databaseSettings) return databaseSettings;

  return {
    ...databaseSettings,
    ysqlServerRpcPort: ports.ysqlServerRpcPort,
    internalYsqlServerRpcPort: ports.internalYsqlServerRpcPort
  };
}

export function getConnectionPoolingPortsFromAdvanced(
  otherAdvancedSettings?: OtherAdvancedProps
): ConnectionPoolingPortFields {
  return {
    ysqlServerRpcPort:
      otherAdvancedSettings?.ysqlServerRpcPort ?? DEFAULT_CONNECTION_POOLING_PORTS.ysqlServerRpcPort,
    internalYsqlServerRpcPort:
      otherAdvancedSettings?.internalYsqlServerRpcPort ??
      DEFAULT_CONNECTION_POOLING_PORTS.internalYsqlServerRpcPort
  };
}

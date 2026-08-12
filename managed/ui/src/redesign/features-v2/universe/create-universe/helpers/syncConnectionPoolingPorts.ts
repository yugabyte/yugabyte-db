import { CloudType } from '@app/redesign/helpers/dtos';
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

/** K8s does not support customizing deployment / connection-pooling ports. */
export function canOverrideCommunicationPorts(
  providerCode?: string | CloudType | null
): boolean {
  return providerCode !== CloudType.kubernetes;
}

/** True only when CP + override ports are enabled and the provider allows port overrides. */
export function shouldApplyConnectionPoolingPortOverrides(
  databaseSettings?: Pick<DatabaseSettingsProps, 'enableConnectionPooling' | 'overrideCPPorts'>,
  providerCode?: string | CloudType | null
): boolean {
  return (
    canOverrideCommunicationPorts(providerCode) &&
    !!databaseSettings?.enableConnectionPooling &&
    !!databaseSettings?.overrideCPPorts
  );
}

/** Clear CP port overrides (used when provider is K8s or override is unavailable). */
export function clearConnectionPoolingPortOverrides(
  databaseSettings: DatabaseSettingsProps
): DatabaseSettingsProps {
  return {
    ...databaseSettings,
    overrideCPPorts: false,
    ...DEFAULT_CONNECTION_POOLING_PORTS
  };
}

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

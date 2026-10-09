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
export function canOverrideCommunicationPorts(providerCode?: string | CloudType | null): boolean {
  return providerCode !== CloudType.kubernetes;
}

/**
 * YSQL + Internal YSQL stay in sync between Database Settings and Advanced
 * whenever the provider allows port customization (not K8s).
 */
export function shouldSyncConnectionPoolingPorts(
  providerCode?: string | CloudType | null
): boolean {
  return canOverrideCommunicationPorts(providerCode);
}

/**
 * Custom Internal YSQL is kept only while connection pooling is enabled.
 * Turning CP off resets Internal YSQL to the default.
 */
export function shouldKeepCustomInternalYsqlPort(
  databaseSettings?: Pick<DatabaseSettingsProps, 'enableConnectionPooling'>,
  providerCode?: string | CloudType | null
): boolean {
  return canOverrideCommunicationPorts(providerCode) && !!databaseSettings?.enableConnectionPooling;
}

/** Alias for shouldKeepCustomInternalYsqlPort. */
export function shouldApplyConnectionPoolingPortOverrides(
  databaseSettings?: Pick<DatabaseSettingsProps, 'enableConnectionPooling' | 'overrideCPPorts'>,
  providerCode?: string | CloudType | null
): boolean {
  return shouldKeepCustomInternalYsqlPort(databaseSettings, providerCode);
}

/** Ports to copy between Database and Advanced. Internal YSQL is default when CP is off. */
export function resolveConnectionPoolingPorts(
  ports: Partial<ConnectionPoolingPortFields> | undefined,
  enableConnectionPooling: boolean | undefined
): ConnectionPoolingPortFields {
  return {
    ysqlServerRpcPort:
      ports?.ysqlServerRpcPort ?? DEFAULT_CONNECTION_POOLING_PORTS.ysqlServerRpcPort,
    internalYsqlServerRpcPort: enableConnectionPooling
      ? (ports?.internalYsqlServerRpcPort ??
        DEFAULT_CONNECTION_POOLING_PORTS.internalYsqlServerRpcPort)
      : DEFAULT_CONNECTION_POOLING_PORTS.internalYsqlServerRpcPort
  };
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

/** Copy CP ports into Advanced deployment ports. */
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
      otherAdvancedSettings?.ysqlServerRpcPort ??
      DEFAULT_CONNECTION_POOLING_PORTS.ysqlServerRpcPort,
    internalYsqlServerRpcPort:
      otherAdvancedSettings?.internalYsqlServerRpcPort ??
      DEFAULT_CONNECTION_POOLING_PORTS.internalYsqlServerRpcPort
  };
}

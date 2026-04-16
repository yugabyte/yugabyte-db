import { CommunicationPorts } from '../steps/database-settings/dtos';

export const DEFAULT_COMMUNICATION_PORTS: CommunicationPorts = {
  masterHttpPort: 7000,
  masterRpcPort: 7100,
  tserverHttpPort: 9000,
  tserverRpcPort: 9100,
  redisServerHttpPort: 11000,
  redisServerRpcPort: 6379,
  yqlServerHttpPort: 12000,
  yqlServerRpcPort: 9042,
  ysqlServerHttpPort: 13000,
  ysqlServerRpcPort: 5433,
  internalYsqlServerRpcPort: 6433,
  nodeExporterPort: 9300,
  ybControllerrRpcPort: 18018
};

export const ArchitectureType = {
  X86_64: 'x86_64',
  ARM64: 'aarch64'
} as const;
export type ArchitectureType = typeof ArchitectureType[keyof typeof ArchitectureType];

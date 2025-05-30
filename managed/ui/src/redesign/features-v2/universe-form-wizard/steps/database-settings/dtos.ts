import { UniverseInfo, UniverseSpec } from '../../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';

export interface DatabaseSettingsProps {
  ysql?: UniverseSpec['ysql'];
  ycql?: UniverseSpec['ycql'];
  enableConnectionPooling?: boolean;
  overrideCPPorts?: boolean;
  ysqlServerRpcPort?: number;
  internalYsqlServerRpcPort?: number;
  enablePGCompatibitilty?: boolean;
  ysql_confirm_password?: string;
  ycql_confirm_password?: string;
}
export interface CommunicationPorts {
  masterHttpPort: number;
  masterRpcPort: number;
  tserverHttpPort: number;
  tserverRpcPort: number;
  redisServerHttpPort: number;
  redisServerRpcPort: number;
  yqlServerHttpPort: number;
  yqlServerRpcPort: number;
  ysqlServerHttpPort: number;
  ysqlServerRpcPort: number;
  nodeExporterPort: number;
  internalYsqlServerRpcPort?: number;
  ybControllerrRpcPort: number;
}

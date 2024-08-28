import { Box } from '@material-ui/core';

interface CommunicationPortsProps {
  communicationPorts: Record<string, string>;
}

const COMMUNICATION_PORTS_KEYS = {
  masterHttpPort: 'Master HTTP Port',
  masterRpcPort: 'Master RPC Port',
  tserverHttpPort: 'TServer HTTP Port',
  tserverRpcPort: 'TServer RPC Port',
  redisServerHttpPort: 'Redis Server HTTP Port',
  redisServerRpcPort: 'Redis Server RPC Port',
  yqlServerHttpPort: 'YQL Server HTTP Port',
  yqlServerRpcPort: 'YQL Server RPC Port',
  ysqlServerHttpPort: 'YSQL Server HTTP Port',
  ysqlServerRpcPort: 'YSQL Server RPC Port',
  nodeExporterPort: 'Node Exporter Port'
};
export const CommunicationPorts = ({ communicationPorts }: CommunicationPortsProps) => {
  return (
    <>
      {Object.entries(communicationPorts ?? {}).map(([key, value]) => {
        const portKey = COMMUNICATION_PORTS_KEYS[key];
        return (
          <Box key={key}>
            <span>{`${portKey}: ${value}`}</span>
          </Box>
        );
      })}
    </>
  );
};

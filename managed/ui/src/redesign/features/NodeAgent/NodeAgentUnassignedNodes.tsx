import { FC, useEffect, useState } from 'react';
import { useMutation } from 'react-query';
import { NodeAgentAPI } from './api';
import { NodeAgentData } from './NodeAgentData';
import { Provider } from '../../helpers/dtos';
import { NodeAgentEntities, ProviderNode, SortDirection } from '../../utils/dtos';
import { MetricConsts } from '../../../components/metrics/constants';
import { isNonEmptyArray } from '../../../utils/ObjectUtils';

interface NodeAgentUnassignedNodesProps {
  isNodeAgentDebugPage: boolean;
  nodeIPs: string[];
  // In case universe selected is 'All Providers', we use string data type
  selectedProvider?: Provider | typeof MetricConsts.ALL | ProviderNode[];
}

export const NodeAgentUnassignedNodes: FC<NodeAgentUnassignedNodesProps> = ({
  isNodeAgentDebugPage,
  nodeIPs,
  selectedProvider
}) => {
  const [isNodeAgentDeleted, setNodeAgentDeleted] = useState<boolean>(false);
  const [nodeAgentData, setNodeAgentData] = useState<NodeAgentEntities[]>([]);
  const nodeAgentStatusByIPs = useMutation(
    (queryParams) => NodeAgentAPI.fetchNodeAgentByIPs(queryParams),
    {
      onSuccess: (data) => {
        setNodeAgentData(data.entities);
      }
    }
  );

  const filter =
    selectedProvider === MetricConsts.ALL
      ? { cloudType: 'onprem' }
      : {
          nodeIps: nodeIPs
        };
  const payload: any = {
    filter: filter,
    sortBy: 'ip',
    direction: SortDirection.DESC,
    offset: 0,
    limit: 500,
    needTotalCount: true
  };

  const onNodeAgentDeleted = () => {
    setNodeAgentDeleted(true);
  };
  // Call page API when nodeIPs change
  useEffect(() => {
    if (isNonEmptyArray(nodeIPs)) {
      nodeAgentStatusByIPs.mutateAsync(payload);
    }
  }, [nodeIPs]);

  // Call page API when provider selected is "All Providers"
  useEffect(() => {
    if (selectedProvider === MetricConsts.ALL || isNodeAgentDeleted) {
      nodeAgentStatusByIPs.mutateAsync(payload);
    }
  }, [selectedProvider, isNodeAgentDeleted]);

  return (
    <NodeAgentData
      isAssignedNodes={false}
      nodeAgentData={nodeAgentData}
      isNodeAgentDebugPage={isNodeAgentDebugPage}
      onNodeAgentDeleted={onNodeAgentDeleted}
    />
  );
};

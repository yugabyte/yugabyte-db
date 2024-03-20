import { FC, useEffect, useState } from 'react';
import { useQuery, useMutation } from 'react-query';
import { NodeAgentAPI, QUERY_KEY } from './api';
import { NodeAgentData } from './NodeAgentData';
import { YBErrorIndicator, YBLoading } from '../../../components/common/indicators';
import { isNonEmptyArray } from '../../../utils/ObjectUtils';
import { NodeAgentEntities, SortDirection } from '../../utils/dtos';
import { MetricConsts } from '../../../components/metrics/constants';
import { Universe } from '../../helpers/dtos';

const IP = 'ip';
interface NodeAgentAssignedNodesProps {
  isNodeAgentDebugPage: boolean;
  nodeIPs: string[];
  // In case universe selected is 'All Universes', we use string data type
  selectedUniverse?: Universe | typeof MetricConsts.ALL;
}

export const NodeAgentAssignedNodes: FC<NodeAgentAssignedNodesProps> = ({
  isNodeAgentDebugPage,
  nodeIPs,
  selectedUniverse
}) => {
  const [nodeAgentData, setNodeAgentData] = useState<NodeAgentEntities[]>([]);
  const [isAPIError, setIsAPIError] = useState<boolean>(false);

  const nodeAgentStatusByIPs = useMutation(
    (queryParams) => NodeAgentAPI.fetchNodeAgentByIPs(queryParams),
    {
      onSuccess: (data) => {
        setNodeAgentData(data.entities);
        setIsAPIError(false);
      },
      onError: () => {
        setIsAPIError(true);
      }
    }
  );

  const allNodeAgents = useQuery(QUERY_KEY.fetchNodeAgents, () => NodeAgentAPI.fetchNodeAgents(), {
    onSuccess: (data) => {
      setNodeAgentData(data);
      setIsAPIError(false);
    },
    onError: () => {
      setIsAPIError(true);
    }
  });

  useEffect(() => {
    if (selectedUniverse === MetricConsts.ALL) {
      allNodeAgents.refetch();
      // Node IP list is based on Universe selected or specific regions/zones
    } else if (isNonEmptyArray(nodeIPs)) {
      const payload: any = {
        filter: {
          nodeIps: nodeIPs
        },
        sortBy: IP,
        direction: SortDirection.DESC,
        offset: 0,
        limit: 500,
        needTotalCount: true
      };
      nodeAgentStatusByIPs.mutateAsync(payload);
    }
  }, [nodeIPs, selectedUniverse === MetricConsts.ALL]);

  if (allNodeAgents.isLoading || nodeAgentStatusByIPs.isLoading) {
    return <YBLoading />;
  }

  if (isAPIError) {
    return <YBErrorIndicator />;
  }

  return (
    <NodeAgentData
      isAssignedNodes={true}
      nodeAgentData={nodeAgentData}
      isNodeAgentDebugPage={isNodeAgentDebugPage}
    />
  );
};

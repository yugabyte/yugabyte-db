import { useGetClusterNodesQuery, useGetIsLoadBalancerIdleQuery } from "@app/api/src";
import React from "react";

export const useNodes = (getAllMasters?: boolean) => {
  // Get nodes
  const {
    data: nodesResponse,
    refetch: refetchClusterNodesQuery,
    ...nodeQueryData
  } = useGetClusterNodesQuery(
    { ...getAllMasters && {get_all_masters: getAllMasters} }
  );

  // We get load balancer separately for now since we rely on yb-admin which is slow
  const {
    data: isLoadBalancerIdleResponse,
    isFetching: fetchingIsLoadBalancerIdle,
    refetch: refetchIsLoadBalancerIdle,
  } = useGetIsLoadBalancerIdleQuery();

  const refetch = React.useCallback(() => {
    refetchClusterNodesQuery();
    refetchIsLoadBalancerIdle();
  }, [refetchClusterNodesQuery, refetchIsLoadBalancerIdle]);

  const updatedNodeResponse = React.useMemo<typeof nodesResponse>(() => {
    if (!nodesResponse) {
      return nodesResponse;
    }

    return {
      data: nodesResponse.data.map((node) => {
        return {
          ...node,
          is_bootstrapping: fetchingIsLoadBalancerIdle
            ? false
            : !node.is_node_up
            ? false
            : (node.metrics.uptime_seconds < 60 && !isLoadBalancerIdleResponse?.is_idle) &&
              (node.metrics.user_tablets_total + node.metrics.system_tablets_total == 0),
        };
      }),
    };
  }, [nodesResponse, fetchingIsLoadBalancerIdle, isLoadBalancerIdleResponse]);

  return { data: updatedNodeResponse, refetch, ...nodeQueryData };
};

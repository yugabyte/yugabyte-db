import React, { FC, useEffect, useState } from 'react';
import { useMutation } from 'react-query';
import { toast } from 'react-toastify';
import { NodeAgentAPI } from './api';
import { NODE_AGENT_TABS } from './NodeAgentConfig';
import { NodeAgentAssignedNodes } from './NodeAgentAssignedNodes';
import { NodeAgentUnassignedNodes } from './NodeAgentUnassignedNodes';
import { UniverseSelector } from '../../../components/metrics/MetricsComparisonModal/UniverseSelector';
import { ProviderSelector } from '../../../components/metrics/MetricsComparisonModal/ProviderSelector';
import { RegionSelector } from '../../../components/metrics/MetricsComparisonModal/RegionSelector';
import { NodeSelector } from '../../../components/metrics/MetricsComparisonModal/NodeSelector';
import { Provider, Universe } from '../../helpers/dtos';
import { ProviderNode } from '../../utils/dtos';
import { NodeDetails } from '../universe/universe-form/utils/dto';
import { MetricConsts, NodeType } from '../../../components/metrics/constants';
import { isNonEmptyObject } from '../../../utils/ObjectUtils';
import '../../../components/metrics/GraphPanelHeader/GraphPanelHeader.scss';
import './NodeAgent.scss';

interface NodeAgentHeaderProps {
  tabKey: string;
}

export const NodeAgentHeader: FC<NodeAgentHeaderProps> = ({ tabKey }) => {
  const [universe, setUniverse] = useState<typeof MetricConsts.ALL | Universe>(MetricConsts.ALL);
  const [universeName, setUniverseName] = useState<string>(MetricConsts.ALL);
  const [universeUUID, setUniverseUUID] = useState<string>(MetricConsts.ALL);
  const [provider, setProvider] = useState<any>(MetricConsts.ALL);
  const [providerName, setProviderName] = useState<string>(MetricConsts.ALL);
  const [providerUUID, setProviderUUID] = useState<string>(MetricConsts.ALL);
  const [region, setRegion] = useState<string>(MetricConsts.ALL);
  const [regionClusterUUID, setRegionClusterUUID] = useState<string | null>(null);
  const [regionCode, setRegionCode] = useState<string | null>(null);
  const [nodeName, setNodeName] = useState<string>(MetricConsts.ALL);
  const [zoneName, setZoneName] = useState<string | null>(null);
  const [nodeDetails, setNodeDetails] = useState<any>([]);
  const [nodeIPs, setNodeIPs] = useState<string[]>([]);

  // Get the list of OnPrem providers to display on Provider dropdown list
  const onPremProviderNodeList = useMutation((providerUUID: string) =>
    NodeAgentAPI.fetchOnPremProviderNodeList(providerUUID)
  );

  // A specific provider or 'All Providers' is selected in this case
  const onProviderChanged = (provider: Provider) => {
    // TODO: Handle "All Providers after Khogen change is merged"
    setProviderName(isNonEmptyObject(provider) ? provider.name : MetricConsts.ALL);
    setProviderUUID(isNonEmptyObject(provider) ? provider.uuid : MetricConsts.ALL);

    setNodeName(MetricConsts.ALL);
    setRegion(MetricConsts.ALL);
    setRegionCode(null);
    setZoneName(null);
    setRegionClusterUUID(null);
  };

  // A specific universe or 'All Universes' is selected in this case
  const onUniverseChanged = (universe: Universe) => {
    setUniverse(isNonEmptyObject(universe) ? universe : MetricConsts.ALL);
    setNodeDetails(isNonEmptyObject(universe) ? universe.universeDetails.nodeDetailsSet : []);
    setUniverseName(isNonEmptyObject(universe) ? universe.name : MetricConsts.ALL);
    setUniverseUUID(isNonEmptyObject(universe) ? universe.universeUUID : MetricConsts.ALL);

    setNodeName(MetricConsts.ALL);
    setRegion(MetricConsts.ALL);
    setRegionCode(null);
    setZoneName(null);
    setRegionClusterUUID(null);
  };

  // A specific region is selected in this case
  const onRegionChanged = (region: string, regionCode?: string | null, clusterId?: string) => {
    setRegion(region);
    setRegionCode(regionCode!);
    setRegionClusterUUID(clusterId!);
    setZoneName(null);
    setNodeName(MetricConsts.ALL);
  };

  // A specific node/zone is selected in this case
  const onNodeItemChanged = (nodeName: string, zoneName?: string | null, nodeItems?: any) => {
    setNodeDetails(nodeItems);
    setNodeName(nodeName);
    setZoneName(zoneName!);
  };

  useEffect(() => {
    let selectedNodeDetails = nodeDetails;
    // AssignedNodes handles Universe Dropdown
    if (tabKey === NODE_AGENT_TABS.AssignedNodes) {
      if (universeName !== MetricConsts.ALL) {
        // Filter Node IPs based on zones or specific nodes selected
        if (nodeName !== MetricConsts.ALL || zoneName) {
          selectedNodeDetails = selectedNodeDetails.filter((nodeDetail: NodeDetails) =>
            zoneName ? zoneName === nodeDetail.cloudInfo?.az : nodeName === nodeDetail.nodeName
          );
          // Filter Node IPs based on regions selected
        } else if (
          nodeName === MetricConsts.ALL &&
          !zoneName &&
          (regionCode || regionClusterUUID)
        ) {
          selectedNodeDetails = selectedNodeDetails.filter((nodeDetail: NodeDetails) =>
            regionCode
              ? regionClusterUUID === nodeDetail.placementUuid &&
                nodeDetail.cloudInfo?.region === regionCode
              : regionClusterUUID === nodeDetail.placementUuid
          );
        }
        const nodeIps = selectedNodeDetails?.map(
          (nodeDetail: NodeDetails) => nodeDetail?.cloudInfo?.private_ip
        );
        setNodeIPs(nodeIps);
      }
      // UnassignedNodes handles Provider Dropdown
    } else if (tabKey === NODE_AGENT_TABS.UnassignedNodes) {
      // Filter Node IPs based on region selected
      if (region !== MetricConsts.ALL && nodeName === MetricConsts.ALL) {
        selectedNodeDetails = selectedNodeDetails.filter(
          (nodeItem: ProviderNode) => nodeItem.details.region === region
        );
        // Filter Node IPs based on nodes selected
      } else if (nodeName !== MetricConsts.ALL) {
        selectedNodeDetails = selectedNodeDetails.filter(
          (nodeItem: ProviderNode) => nodeItem.details.ip === nodeName
        );
      }
      const nodeIps = selectedNodeDetails?.map((nodeItem: ProviderNode) => nodeItem.details.ip);
      setNodeIPs(nodeIps);
    }
  }, [region, regionCode, universeName, regionClusterUUID, nodeName, zoneName]);

  // When any specific provider or "All Providers" is selected, call appropriate API
  useEffect(() => {
    if (providerName !== MetricConsts.ALL) {
      const fetchData = async () => {
        try {
          const onPremNodeList = await onPremProviderNodeList.mutateAsync(providerUUID);
          const nodeIps = onPremNodeList?.map((nodeItem: ProviderNode) => nodeItem.details.ip);
          setProvider(onPremNodeList);
          setNodeDetails(onPremNodeList);
          setNodeIPs(nodeIps);
        } catch (e) {
          toast.error('Fetching Provider node list failed');
        }
      };
      fetchData();
    } else {
      setProvider(MetricConsts.ALL);
    }
  }, [providerName]);

  return (
    <>
      <div className="node-agent-panel">
        {tabKey === NODE_AGENT_TABS.AssignedNodes && (
          <UniverseSelector
            onUniverseChanged={onUniverseChanged}
            currentSelectedUniverse={universeName}
            currentSelectedUniverseUUID={universeUUID}
          />
        )}
        {tabKey === NODE_AGENT_TABS.UnassignedNodes && (
          <ProviderSelector
            onProviderChanged={onProviderChanged}
            currentSelectedProvider={providerName}
            currentSelectedProviderUUID={providerUUID}
          />
        )}
        <RegionSelector
          selectedUniverse={universe}
          onRegionChanged={onRegionChanged}
          currentSelectedRegion={region}
          selectedRegionClusterUUID={regionClusterUUID}
          filterRegionsByUniverse={tabKey === NODE_AGENT_TABS.AssignedNodes}
          selectedProvider={provider}
        />
        <NodeSelector
          selectedUniverse={universe}
          nodeItemChanged={onNodeItemChanged}
          selectedNode={nodeName}
          selectedRegionClusterUUID={regionClusterUUID}
          selectedZoneName={zoneName}
          isDedicatedNodes={false}
          selectedRegionCode={regionCode}
          currentSelectedNodeType={NodeType.ALL}
          selectedRegion={region}
          filterRegionsByUniverse={tabKey === NODE_AGENT_TABS.AssignedNodes}
          selectedProvider={provider}
        />
      </div>
      {tabKey === NODE_AGENT_TABS.AssignedNodes && (
        <NodeAgentAssignedNodes
          nodeIPs={nodeIPs}
          selectedUniverse={universe}
          isNodeAgentDebugPage={true}
        />
      )}
      {tabKey === NODE_AGENT_TABS.UnassignedNodes && (
        <NodeAgentUnassignedNodes
          nodeIPs={nodeIPs}
          selectedProvider={provider}
          isNodeAgentDebugPage={true}
        />
      )}
    </>
  );
};

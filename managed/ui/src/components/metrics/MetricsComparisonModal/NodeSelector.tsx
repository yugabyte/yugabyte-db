import { FC } from 'react';
import { Dropdown, MenuItem } from 'react-bootstrap';
import { MetricConsts, NodeType } from '../../metrics/constants';
import { isNonEmptyObject, isNonEmptyString } from '../../../utils/ObjectUtils';
import { getReadOnlyCluster } from '../../../utils/UniverseUtils';

interface NodeSelectorProps {
  nodeItemChanged: (nodeName: string, zoneName: string | null, nodeItems?: any) => void;
  currentSelectedNodeType?: string;
  isDedicatedNodes: boolean;
  selectedNode: string;
  selectedRegion?: string | null;
  selectedRegionCode: string | null;
  selectedRegionClusterUUID: string | null;
  selectedZoneName: string | null;
  selectedUniverse: any | null;
  filterRegionsByUniverse?: boolean;
  selectedProvider?: any | null;
}

export const NodeSelector: FC<NodeSelectorProps> = ({
  selectedUniverse,
  nodeItemChanged,
  selectedNode,
  selectedRegionClusterUUID,
  selectedZoneName,
  isDedicatedNodes,
  selectedRegion,
  selectedRegionCode,
  currentSelectedNodeType,
  filterRegionsByUniverse = true,
  selectedProvider
}) => {
  let nodeItems: any[] = [];
  let nodeItemsElement: any = [];
  let zone = '';
  let renderItem = null;
  let nodeData = null;
  let hasSelectedReadReplica = false;

  const isDisabled = filterRegionsByUniverse
    ? selectedUniverse === MetricConsts.ALL
    : selectedProvider === MetricConsts.ALL;

  if (filterRegionsByUniverse) {
    if (
      isNonEmptyObject(selectedUniverse) &&
      selectedUniverse !== MetricConsts.ALL &&
      selectedUniverse.universeDetails.nodeDetailsSet
    ) {
      nodeItems = selectedUniverse.universeDetails.nodeDetailsSet.sort((a: any, b: any) => {
        if (a.cloudInfo.az === null) {
          return -1;
        } else if (b.cloudInfo.az === null) {
          return 1;
        } else {
          return a.cloudInfo.az.toLowerCase() < b.cloudInfo.az.toLowerCase() ? -1 : 1;
        }
      });
    }

    // Show nodes based on the region selected (we filter this by cluster id)
    if (selectedRegionClusterUUID) {
      const readReplicaCluster = getReadOnlyCluster(selectedUniverse.universeDetails.clusters);
      const readReplicaClusterUUID = readReplicaCluster?.uuid;
      hasSelectedReadReplica = selectedRegionClusterUUID === readReplicaClusterUUID;

      nodeItems = nodeItems.filter((nodeItem: any) =>
        selectedRegionCode
          ? selectedRegionClusterUUID === nodeItem.placementUuid &&
            nodeItem.cloudInfo?.region === selectedRegionCode
          : selectedRegionClusterUUID === nodeItem.placementUuid
      );
    }

    // Show nodes based on node type in case of Primary cluster
    if (currentSelectedNodeType !== NodeType.ALL && !hasSelectedReadReplica) {
      nodeItems = nodeItems.filter((nodeItem: any) =>
        currentSelectedNodeType === NodeType.MASTER
          ? nodeItem.dedicatedTo === NodeType.MASTER.toUpperCase()
          : nodeItem.dedicatedTo === NodeType.TSERVER.toUpperCase() || nodeItem.isTserver
      );
    }

    // eslint-disable-next-line react/display-name
    nodeItemsElement = nodeItems?.map((nodeItem: any, nodeIdx: number) => {
      let zoneNameElement = null;
      let zoneDividerElement = null;
      const nodeKey = `${nodeItem.nodeName}-node-${nodeIdx}`;
      const zoneKey = `${nodeItem.cloudInfo.az}-zone-${nodeIdx}`;
      // Logic to decide when AZ and divider needs to be shown
      if (zone !== nodeItem.cloudInfo.az) {
        zoneDividerElement = <div id="zone-divider" className="divider" />;
        zoneNameElement = (
          <MenuItem
            onSelect={() => nodeItemChanged(MetricConsts.ALL, nodeItem.cloudInfo.az, nodeItems)}
            key={zoneKey}
            // Added this line due to the issue that dropdown does not close
            // when a menu item is selected
            onClick={() => {
              document.body.click();
            }}
            eventKey={nodeItem.cloudInfo.az}
            active={selectedZoneName === nodeItem.cloudInfo.az}
          >
            <span className="cluster-az-name">{nodeItem.cloudInfo.az}</span>
          </MenuItem>
        );
        zone = nodeItem.cloudInfo.az;
      }

      return (
        // eslint-disable-next-line react/jsx-key
        <>
          {zoneDividerElement}
          {zoneNameElement}
          <MenuItem
            onSelect={() => nodeItemChanged(nodeItem.nodeName, null, nodeItems)}
            key={nodeKey}
            // Added this line due to the issue that dropdown does not close
            // when a menu item is selected
            onClick={() => {
              document.body.click();
            }}
            eventKey={nodeIdx}
            active={selectedNode === nodeItem.nodeName}
          >
            <span className={'node-name'}>
              {nodeItem.nodeName}
              &nbsp;
            </span>
            {isDedicatedNodes ? (
              <span className={'node-type-label'}>
                {nodeItem.dedicatedTo?.toLowerCase() ?? NodeType.TSERVER.toLowerCase()}
              </span>
            ) : null}
            &nbsp;&nbsp;
            <span className={'node-ip-address'}>{nodeItem.cloudInfo.private_ip}</span>
          </MenuItem>
        </>
      );
    });
  } else {
    // eslint-disable-next-line no-lonely-if
    if (isNonEmptyObject(selectedProvider) && selectedProvider !== MetricConsts.ALL) {
      const filteredProvidersByRegion =
        selectedRegion !== MetricConsts.ALL
          ? selectedProvider.filter(
              (providerNode: any) => providerNode.details.region === selectedRegion
            )
          : selectedProvider;
      nodeItemsElement = filteredProvidersByRegion?.map(
        // eslint-disable-next-line react/display-name
        (providerNode: any, providerNodeIdx: number) => {
          const nodeIP = providerNode.details.ip;
          const nodeName = providerNode.details.nodeName;
          const nodeKey = `${nodeName.nodeName}-node-${providerNodeIdx}`;
          return (
            <MenuItem
              onSelect={() => nodeItemChanged(nodeIP, null, filteredProvidersByRegion)}
              key={nodeKey}
              eventKey={providerNodeIdx}
              active={selectedNode === nodeIP}
            >
              <span className={'node-name'}>
                {isNonEmptyString(nodeName) ? nodeName : nodeIP}
                &nbsp;
              </span>
              &nbsp;&nbsp;
              {isNonEmptyString(nodeName) && <span className={'node-ip-address'}>{nodeIP}</span>}
            </MenuItem>
          );
        }
      );
    }
  }

  // By default we need to have 'All nodes' populated
  const defaultMenuItem = (
    <MenuItem
      onSelect={() =>
        nodeItemChanged(
          MetricConsts.ALL,
          null,
          filterRegionsByUniverse ? nodeItems : selectedProvider
        )
      }
      key={MetricConsts.ALL}
      active={selectedNode === MetricConsts.ALL && !selectedZoneName}
      eventKey={MetricConsts.ALL}
    >
      {'All AZs & nodes'}
    </MenuItem>
  );
  nodeItemsElement.splice(0, 0, defaultMenuItem);

  if (selectedZoneName) {
    renderItem = selectedZoneName;
  } else if (selectedNode && selectedNode !== MetricConsts.ALL) {
    renderItem = selectedNode;
  } else {
    renderItem = nodeItemsElement[0];
  }

  nodeData = (
    <div className="node-picker-container">
      <Dropdown
        id="nodeFilterDropdown"
        className="node-filter-dropdown"
        disabled={isDisabled}
        title={isDisabled ? 'Select a specific universe to view the zones and nodes' : ''}
      >
        <Dropdown.Toggle className="dropdown-toggle-button node-filter-dropdown__border-topk">
          <span className="default-value">{renderItem}</span>
        </Dropdown.Toggle>
        <Dropdown.Menu>{nodeItemsElement.length > 1 && nodeItemsElement}</Dropdown.Menu>
      </Dropdown>
    </div>
  );

  return nodeData;
};

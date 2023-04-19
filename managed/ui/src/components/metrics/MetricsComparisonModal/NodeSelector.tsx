import React, { FC } from 'react';
import { Dropdown, MenuItem } from 'react-bootstrap';
import { MetricConsts, NodeType } from '../../metrics/constants';
import { isNonEmptyObject } from '../../../utils/ObjectUtils';
import { getReadOnlyCluster } from '../../../utils/UniverseUtils';

interface NodeSelectorData {
  nodeItemChanged: (nodeName: string, zoneName: string | null) => void;
  nodeItemChangedOld: (nodeName: string) => void;
  currentSelectedNodeType?: string;
  isDedicatedNodes: boolean;
  isTopKMetricsEnabled: boolean;
  selectedNode: string;
  selectedRegionCode: string;
  selectedRegionClusterUUID: string | null;
  selectedZoneName: string | null;
  selectedUniverse: any | null;
}

export const NodeSelector: FC<NodeSelectorData> = ({
  selectedUniverse,
  nodeItemChanged,
  nodeItemChangedOld,
  selectedNode,
  selectedRegionClusterUUID,
  selectedZoneName,
  isDedicatedNodes,
  isTopKMetricsEnabled,
  selectedRegionCode,
  currentSelectedNodeType
}) => {
  let nodeItems: any[] = [];
  let nodeItemsElement: any = [];
  let zone = '';
  let renderItem = null;
  let nodeData = null;
  let hasSelectedReadReplica = false;
  const isDisabled = selectedUniverse === MetricConsts.ALL;

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

  if (isTopKMetricsEnabled) {
    const readReplicaCluster = getReadOnlyCluster(selectedUniverse.universeDetails.clusters);
    const readReplicaClusterUUID = readReplicaCluster?.uuid;
    hasSelectedReadReplica = selectedRegionClusterUUID === readReplicaClusterUUID;

    // Show nodes based on the region selected (we filter this by cluster id)
    if (selectedRegionClusterUUID) {
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
            onSelect={() => nodeItemChanged(MetricConsts.ALL, nodeItem.cloudInfo.az)}
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
            onSelect={() => nodeItemChanged(nodeItem.nodeName, null)}
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

    // By default we need to have 'All nodes' populated
    const defaultMenuItem = (
      <MenuItem
        onSelect={() => nodeItemChanged(MetricConsts.ALL, null)}
        key={MetricConsts.ALL}
        // Added this line due to the issue that dropdown does not close
        // when a menu item is selected
        active={selectedNode === MetricConsts.ALL && !selectedZoneName}
        onClick={() => {
          document.body.click();
        }}
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
  } else {
    // eslint-disable-next-line react/display-name
    nodeItemsElement = nodeItems?.map((nodeItem: any, nodeIdx: number) => {
      return (
        <MenuItem
          onSelect={() => nodeItemChangedOld(nodeItem.nodeName)}
          key={nodeIdx}
          // Added this line due to the issue that dropdown does not close
          // when a menu item is selected
          onClick={() => {
            document.body.click();
          }}
          eventKey={nodeIdx}
          active={selectedNode === nodeItem.nodeName}
        >
          <span className={'node-name'}>{nodeItem.nodeName}</span>
          &nbsp;&nbsp;
          <span className={'node-ip-address'}>{nodeItem.cloudInfo.private_ip}</span>
        </MenuItem>
      );
    });
    // By default we need to have 'All nodes' populated
    const defaultMenuOldItem = (
      <MenuItem
        onSelect={() => nodeItemChangedOld(MetricConsts.ALL)}
        key={MetricConsts.ALL}
        // Added this line due to the issue that dropdown does not close
        // when a menu item is selected
        active={selectedNode === MetricConsts.ALL}
        onClick={() => {
          document.body.click();
        }}
        eventKey={MetricConsts.ALL}
      >
        {'All'}
      </MenuItem>
    );
    nodeItemsElement.splice(0, 0, defaultMenuOldItem);

    if (selectedNode && selectedNode !== MetricConsts.ALL) {
      renderItem = selectedNode;
    } else {
      renderItem = nodeItemsElement[0];
    }

    nodeData = (
      <div className="node-picker">
        <Dropdown id="nodeFilterDropdown" className="node-filter-dropdown">
          <Dropdown.Toggle className="dropdown-toggle-button node-filter-dropdown__border">
            <span className="default-value">{renderItem}</span>
          </Dropdown.Toggle>
          <Dropdown.Menu>{nodeItemsElement.length > 1 && nodeItemsElement}</Dropdown.Menu>
        </Dropdown>
      </div>
    );
  }
  return nodeData;
};

import { FC } from 'react';
import { Dropdown, MenuItem } from 'react-bootstrap';
import { NodeType } from '../../metrics/constants';
import { isNonEmptyObject } from '../../../utils/ObjectUtils';
import { getReadOnlyCluster } from '../../../utils/UniverseUtils';

const ALL_NODE_TYPES = 'All Node Types';

interface NodeTypeSelectorData {
  onNodeTypeChanged: (nodeType: string) => void;
  currentSelectedNodeType?: string;
  selectedRegionClusterUUID: string;
  selectedRegionCode: string;
  selectedUniverse: any;
}

export const NodeTypeSelector: FC<NodeTypeSelectorData> = ({
  currentSelectedNodeType,
  onNodeTypeChanged,
  selectedUniverse,
  selectedRegionClusterUUID,
  selectedRegionCode
}) => {
  let nodeItems: any[] = [];
  let nodeTypeElement: any = [];
  let isMaster = true;
  let isTServer = true;

  // Master should not be shown in the dropdown in case when user selects RR cluster
  nodeItems = selectedUniverse?.universeDetails?.nodeDetailsSet;
  const readReplicaCluster = getReadOnlyCluster(selectedUniverse.universeDetails.clusters);
  const readReplicaClusterUUID = readReplicaCluster?.uuid;
  const hasSelectedReadReplica = selectedRegionClusterUUID === readReplicaClusterUUID;
  isMaster = !hasSelectedReadReplica;

  if (
    isNonEmptyObject(selectedUniverse) &&
    currentSelectedNodeType &&
    !hasSelectedReadReplica &&
    selectedUniverse.universeDetails.nodeDetailsSet
  ) {
    if (selectedRegionClusterUUID) {
      nodeItems = selectedUniverse.universeDetails.nodeDetailsSet.filter((nodeItem: any) =>
        selectedRegionCode
          ? selectedRegionClusterUUID === nodeItem.placementUuid &&
            nodeItem.cloudInfo?.region === selectedRegionCode
          : selectedRegionClusterUUID === nodeItem.placementUuid
      );
    }

    const masterNodeData = nodeItems.find((nodeItem: any) => {
      return nodeItem.dedicatedTo === NodeType.MASTER.toUpperCase();
    });
    const tserverNodeData = nodeItems.find((nodeItem: any) => {
      return nodeItem.dedicatedTo === NodeType.TSERVER.toUpperCase();
    });

    isMaster = masterNodeData?.isMaster;
    isTServer = tserverNodeData?.isTserver;
  }

  nodeTypeElement = (
    <>
      <MenuItem
        onSelect={() => onNodeTypeChanged(NodeType.ALL)}
        key={NodeType.ALL}
        // Added this line due to the issue that dropdown does not close
        // when a menu item is selected
        active={currentSelectedNodeType === NodeType.ALL}
        onClick={() => {
          document.body.click();
        }}
        eventKey={NodeType.ALL}
      >
        {ALL_NODE_TYPES}
      </MenuItem>
      {isMaster && (
        <MenuItem
          onSelect={() => onNodeTypeChanged(NodeType.MASTER)}
          key={NodeType.MASTER}
          // Added this line due to the issue that dropdown does not close
          // when a menu item is selected
          onClick={() => {
            document.body.click();
          }}
          eventKey={0}
          active={currentSelectedNodeType === NodeType.MASTER}
        >
          <span>{NodeType.MASTER}</span>
        </MenuItem>
      )}
      {isTServer && (
        <MenuItem
          onSelect={() => onNodeTypeChanged(NodeType.TSERVER)}
          key={NodeType.TSERVER}
          // Added this line due to the issue that dropdown does not close
          // when a menu item is selected
          onClick={() => {
            document.body.click();
          }}
          eventKey={0}
          active={currentSelectedNodeType === NodeType.TSERVER}
        >
          <span>{NodeType.TSERVER}</span>
        </MenuItem>
      )}
    </>
  );

  return (
    <div className="node-type-picker-container">
      <Dropdown id="nodeTypeDropdown" className="node-type-filter-dropdown">
        <Dropdown.Toggle className="dropdown-toggle-button">
          <span className="default-value">
            {currentSelectedNodeType === NodeType.ALL ? ALL_NODE_TYPES : currentSelectedNodeType}
          </span>
        </Dropdown.Toggle>
        <Dropdown.Menu>{nodeTypeElement}</Dropdown.Menu>
      </Dropdown>
    </div>
  );
};

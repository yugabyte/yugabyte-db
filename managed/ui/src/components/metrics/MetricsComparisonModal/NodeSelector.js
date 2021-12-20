import React from 'react';
import { FormControl } from 'react-bootstrap';
import { isNonEmptyObject } from '../../../utils/ObjectUtils';

export const NodeSelector = ({
  selectedUniverse,
  nodeItemChanged,
  selectedNode,
  otherSelectedNode
}) => {
  let nodeItems = [];
  if (
    isNonEmptyObject(selectedUniverse) &&
    selectedUniverse !== 'all' &&
    selectedUniverse.universeDetails.nodeDetailsSet
  ) {
    nodeItems = selectedUniverse.universeDetails.nodeDetailsSet.sort((a, b) => {
      if (a.nodeName === null) {
        return -1;
      } else if (b.nodeName === null) {
        return 1;
      } else {
        return a.nodeName.toLowerCase() < b.nodeName.toLowerCase() ? -1 : 1;
      }
    });
  }
  return (
    <div className="node-picker">
      <FormControl componentClass="select" onChange={nodeItemChanged} value={selectedNode}>
        <option key={-1} value="all">
          All
        </option>
        {nodeItems.map((nodeItem, nodeIdx) => (
          <option
            key={nodeIdx}
            value={nodeItem.nodeName}
            disabled={nodeItem.nodeName === otherSelectedNode}
          >
            {nodeItem.nodeName}
            {nodeItem.nodeName === otherSelectedNode ? ' Already selected' : ''}
          </option>
        ))}
      </FormControl>
    </div>
  );
};

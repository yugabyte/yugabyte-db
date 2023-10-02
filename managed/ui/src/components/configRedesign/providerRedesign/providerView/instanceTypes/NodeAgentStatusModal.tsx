import { Box } from '@material-ui/core';
import { YBModal, YBModalProps } from '../../../../../redesign/components';
import { NodeAgentUnassignedNodes } from '../../../../../redesign/features/NodeAgent/NodeAgentUnassignedNodes';
import { NodeAgentAssignedNodes } from '../../../../../redesign/features/NodeAgent/NodeAgentAssignedNodes';

interface NodeAgentStatusModalProps extends YBModalProps {
  nodeIPs: string[];
  onClose: () => void;
  isAssignedNodes: boolean;
}

export const NodeAgentStatusModal = ({
  nodeIPs,
  onClose,
  isAssignedNodes,
  ...modalProps
}: NodeAgentStatusModalProps) => {
  return (
    <YBModal
      size="xl"
      title="Node Agents"
      onClose={onClose}
      titleSeparator
      overrideHeight="fit-content"
      {...modalProps}
    >
      <Box mt={2} paddingBottom={4}>
        {isAssignedNodes ? (
          <NodeAgentAssignedNodes nodeIPs={nodeIPs} isNodeAgentDebugPage={false} />
        ) : (
          <NodeAgentUnassignedNodes nodeIPs={nodeIPs} isNodeAgentDebugPage={false} />
        )}
      </Box>
    </YBModal>
  );
};

import React from 'react';
import { useTranslation } from 'react-i18next';
import { Box, makeStyles } from '@material-ui/core';
import { YBModal, YBModalProps } from '../../../../../redesign/components';
import { NodeAgentUnassignedNodes } from '../../../../../redesign/features/NodeAgent/NodeAgentUnassignedNodes';
import Email from '../../../../../redesign/assets/email.svg';
import { NodeAgentAssignedNodes } from '../../../../../redesign/features/NodeAgent/NodeAgentAssignedNodes';

const useStyles = makeStyles((theme) => ({
  contactSupport: {
    fontWeight: 400,
    fontSize: '13px',
    color: '#44518B',
    textDecoration: 'underline',
    marginLeft: theme.spacing(0.5)
  },
  contactSupportContainer: {
    float: 'right'
  }
}));
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
  const classes = useStyles();
  const { t } = useTranslation();

  return (
    <YBModal
      size="xl"
      title="Node Agents"
      onClose={onClose}
      titleSeparator
      overrideHeight="fit-content"
      {...modalProps}
    >
      <Box className={classes.contactSupportContainer}>
        <img src={Email} alt="email" />
        <span className={classes.contactSupport}>{t('nodeAgent.contactSupport')}</span>
      </Box>
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

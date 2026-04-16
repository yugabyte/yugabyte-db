import clsx from 'clsx';
import { Box, makeStyles, Typography } from '@material-ui/core';
import Check from '../../assets/check-green.svg?img';
import Warning from '../../assets/warning-solid.svg?img';
import Loading from '../../assets/loading-dark.svg?img';

import { NodeAgentState } from '../../utils/dtos';
import { AugmentedNodeAgent } from './types';

const useStyles = makeStyles((theme) => ({
  tagTextBlue: {
    color: '#1A44A5'
  },
  tagTextGreen: {
    color: '#097245'
  },
  tagTextRed: {
    color: '#8F0000'
  },
  tagGreen: {
    backgroundColor: '#CDEFE1'
  },
  tagRed: {
    backgroundColor: '#FDE2E2'
  },
  tagBlue: {
    backgroundColor: '#CBDAFF'
  },
  nodeAgentStateTag: {
    display: 'flex',
    flexDirection: 'row',
    alignItems: 'center',
    height: '24px',
    borderRadius: '6px',
    padding: '10px 6px',
    width: 'fit-content'
  },
  nodeAgentStatusText: {
    fontFamily: 'Inter',
    fontStyle: 'normal',
    fontWeight: 400,
    fontSize: '11.5px',
    lineHeight: theme.spacing(2)
  }
}));

interface NodeAgentStatusProps {
  nodeAgent: AugmentedNodeAgent;
}

export const NodeAgentStatus = ({ nodeAgent }: NodeAgentStatusProps) => {
  const helperClasses = useStyles();
  let tagColor = helperClasses.tagBlue;
  let tagTextColor = helperClasses.tagTextBlue;
  let imageSrc = '';

  if (!nodeAgent.reachable) {
    tagColor = helperClasses.tagRed;
    tagTextColor = helperClasses.tagTextRed;
    imageSrc = Warning;
  } else {
    switch (nodeAgent.state) {
      case NodeAgentState.READY:
        tagColor = helperClasses.tagGreen;
        tagTextColor = helperClasses.tagTextGreen;
        imageSrc = Check;
        break;
      case NodeAgentState.REGISTERING:
      case NodeAgentState.REGISTERED:
        tagColor = helperClasses.tagBlue;
        tagTextColor = helperClasses.tagTextBlue;
        imageSrc = Loading;
        break;
      case NodeAgentState.UPGRADED:
      case NodeAgentState.UPGRADE:
        tagColor = helperClasses.tagBlue;
        tagTextColor = helperClasses.tagTextBlue;
        imageSrc = Loading;
        break;
      default:
        tagColor = helperClasses.tagRed;
        tagTextColor = helperClasses.tagTextRed;
        imageSrc = Warning;
    }
  }

  return (
    <Box className={clsx(helperClasses.nodeAgentStateTag, tagColor)}>
      <Typography variant="body2" className={clsx(helperClasses.nodeAgentStatusText, tagTextColor)}>
        {nodeAgent.statusLabel}
      </Typography>
      <img src={imageSrc} alt="status" />
    </Box>
  );
};

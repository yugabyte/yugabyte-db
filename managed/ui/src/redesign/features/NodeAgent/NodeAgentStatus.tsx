import { FC } from 'react';
import clsx from 'clsx';
import { Box, makeStyles } from '@material-ui/core';
import { NodeAgentStatusList } from '../../utils/dtos';
import Check from '../../assets/check.svg';
import Warning from '../../assets/warning-solid.svg';
import Loading from '../../assets/loading-dark.svg';

const DEFAULT_STATUS_TEXT = 'N/A';

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
  status: string;
  isReachable: boolean;
}

const NODE_AGENT_STATUS_TO_DISPLAY_TEXT = {
  READY: 'Ready',
  REGISTERING: 'Registering',
  UPGRADE: 'Upgrading',
  UPGRADED: 'Upgrading',
  UNREACHABLE: 'Unreachable'
};

export const NodeAgentStatus: FC<NodeAgentStatusProps> = ({ status, isReachable }) => {
  const helperClasses = useStyles();
  let tagColor = helperClasses.tagBlue;
  let tagTextColor = helperClasses.tagTextBlue;
  let imageSrc = '';

  if (!isReachable) {
    tagColor = helperClasses.tagRed;
    tagTextColor = helperClasses.tagTextRed;
    imageSrc = Warning;
  } else {
    switch (status) {
      case NodeAgentStatusList.READY:
        tagColor = helperClasses.tagGreen;
        tagTextColor = helperClasses.tagTextGreen;
        imageSrc = Check;
        break;
      case NodeAgentStatusList.REGISTERING:
        tagColor = helperClasses.tagBlue;
        tagTextColor = helperClasses.tagTextBlue;
        imageSrc = Loading;
        break;
      case NodeAgentStatusList.UPGRADED:
      case NodeAgentStatusList.UPGRADE:
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
      <span className={clsx(helperClasses.nodeAgentStatusText, tagTextColor)}>
        {isReachable
          ? NODE_AGENT_STATUS_TO_DISPLAY_TEXT[status] ?? DEFAULT_STATUS_TEXT
          : NODE_AGENT_STATUS_TO_DISPLAY_TEXT['UNREACHABLE']}
      </span>
      <img src={imageSrc} alt="status" />
    </Box>
  );
};

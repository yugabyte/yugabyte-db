import { Box, makeStyles } from '@material-ui/core';
import clsx from 'clsx';

import TipIcon from '../../../assets/Tip.svg';

interface YBBannerProps {
  bannerClassName?: any;
  message: string;
}

const useStyles = makeStyles(() => ({
  root: {
    padding: '24px 12px 24px 12px',
    borderRadius: '4px',
    backgroundColor: '#495589',
    width: '626px',
    height: '86px'
  },
  messageBox: {
    display: 'flex',
    flexDirection: 'row'
  },
  message: {
    fontFamily: 'Inter',
    fontSize: '15px',
    fontWeight: 400,
    color: '#FFFFFF',
    height: '38px'
  }
}));

export const YBBanner = ({ bannerClassName, message }: YBBannerProps) => {
  const classes = useStyles();

  return (
    <Box className={clsx(bannerClassName, classes.root)}>
      <Box className={classes.messageBox}>
        <img src={TipIcon} alt="tip" />
        <span className={classes.message}>{message}</span>
      </Box>
    </Box>
  );
};

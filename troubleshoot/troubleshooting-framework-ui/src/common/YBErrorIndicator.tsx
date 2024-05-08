import { Box, makeStyles } from '@material-ui/core';
import { ReactComponent as FrownIcon } from '../assets/sad-face.svg';

interface YBErrorIndicatorProps {
  type?: string;
  uuid?: string;
  customErrorMessage?: string;
}

const useStyles = makeStyles({
  errorContainer: {
    textAlign: 'center',
    fontSize: '16px',
    lineHeight: '2em'
  },
  sadFace: {
    width: '40px'
  }
});

export const YBErrorIndicator = ({ type, uuid, customErrorMessage }: YBErrorIndicatorProps) => {
  const classes = useStyles();
  let errorDisplayMessage = <span />;
  let errorRecoveryMessage = <span />;

  if (type === 'universe') {
    errorDisplayMessage = (
      <Box>{`Seems like universe with UUID ${uuid} has issues or does not exist.`}</Box>
    );
  }

  return (
    <Box className={classes.errorContainer}>
      <Box>
        {'Aww Snap.'}
        {/* <FrownIcon className={classes.sadFace} /> */}
      </Box>
      <Box>{errorDisplayMessage}</Box>
      {customErrorMessage && <Box>{customErrorMessage}</Box>}
      <Box>{errorRecoveryMessage}</Box>
    </Box>
  );
};

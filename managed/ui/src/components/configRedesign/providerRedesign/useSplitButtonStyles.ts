import { makeStyles } from '@material-ui/core';

export const useDropdownButtonStyles = makeStyles((theme) => ({
  splitButton: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1),

    height: 32,

    backgroundColor: `${theme.palette.orange[500]} !important`,
    color: `${theme.palette.common.white} !important`,
    '&:hover': {
      backgroundColor: `${theme.palette.orange[700]} !important`
    },
    '&$disabled': {
      backgroundColor: theme.palette.grey[300],
      color: theme.palette.grey[600],
      cursor: 'not-allowed',
      pointerEvents: 'unset',
      '&:hover': {
        backgroundColor: theme.palette.grey[300],
        color: theme.palette.grey[600]
      }
    },
    '& .caret': {
      borderTopColor: theme.palette.common.white
    }
  }
}));

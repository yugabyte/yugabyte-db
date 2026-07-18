import { ReactNode } from 'react';
import { makeStyles, Typography } from '@material-ui/core';

interface YBMenuItemLabelProps {
  label: string;

  preLabelElement?: ReactNode;
  postLabelElement?: ReactNode;
}

const useStyles = makeStyles((theme) => ({
  actionButtonLabel: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(2),

    '& i': {
      // Overriding the defaults appied from
      // .dropdown-menu li i
      width: '15px !important',
      margin: '0 !important'
    },

    '&$disabled': {
      cursor: 'not-allowed'
    }
  },
  disabled: {},
  actionButtonText: {
    fontSize: '14px',
    fontWeight: 300
  }
}));

export const YBMenuItemLabel = ({
  label,
  preLabelElement,
  postLabelElement
}: YBMenuItemLabelProps) => {
  const classes = useStyles();

  return (
    <span className={classes.actionButtonLabel}>
      {!!preLabelElement && preLabelElement}
      <Typography
        className={classes.actionButtonText}
        variant="body2"
        display="inline"
        component="span"
      >
        {label}
      </Typography>
      {!!postLabelElement && postLabelElement}
    </span>
  );
};

import React, { FC, ReactNode } from 'react';
import { FiberManualRecord } from '@material-ui/icons';
import { Box, createStyles, makeStyles, Theme } from '@material-ui/core';
import FailedIcon from '@app/assets/failed-solid.svg';
import CompletedIcon from '@app/assets/check.svg';
import SuccessIcon from '@app/assets/circle-check-solid.svg';
import LoadingIcon from '@app/assets/Default-Loading-Circles.svg';
import WarningIcon from '@app/assets/alert-solid.svg';
import ErrorIcon from '@app/assets/alert-solid.svg';
import { YBTooltip } from '../YBTooltip/YBTooltip';

export enum STATUS_TYPES {
  SUCCESS = 'success',
  FAILED = 'failed',
  WARNING = 'warning',
  COMPLETE = 'completed',
  ACTIVE = 'active',
  INACTIVE = 'inactive',
  PENDING = 'running',
  IN_PROGRESS = 'in_progress',
  ERROR = 'error'
}

interface StatusProps {
  label?: ReactNode;
  type?: STATUS_TYPES;
  value?: number,
  tooltip?: boolean,
}

const useStyles = makeStyles((theme: Theme) => {
  return createStyles({
    root: {
      textAlign: 'center',
      '& > span': {
        margin: theme.spacing(1)
      }
    },
    colorCompleted: {
      marginRight: theme.spacing(0.5),
      color: theme.palette.success.main
    },
    colorSuccess: {
      width: 24,
      marginRight: theme.spacing(0.5),
      color: theme.palette.success.main
    },
    colorError: {
      width: 24,
      marginRight: theme.spacing(0.5),
      color: theme.palette.error.main
    },
    colorWarning: {
      width: 24,
      marginRight: theme.spacing(0.5),
      color: theme.palette.warning.main
    },
    colorInactive: {
      fontSize: 12,
      color: theme.palette.grey[500]
    },
    colorActive: {
      fontSize: 12,
      marginRight: theme.spacing(0.5),
      color: theme.palette.success.main
    },
    colorFailed: {
      width: 24,
      marginRight: theme.spacing(0.5),
      color: theme.palette.error.main
    },
    colorPending: {
      marginRight: theme.spacing(0.5),
      width: 12,
      color: theme.palette.warning[700]
    },
    loadingIcon: {
      width: 21,
      height: 21,
      margin: 0,
      marginRight: theme.spacing(1)
    }
  });
});

export const YBStatus: FC<StatusProps> = ({ label, tooltip, value, type = STATUS_TYPES.COMPLETE }: StatusProps) => {
  const classes = useStyles();

  const getTooltip = () => {
    if (!tooltip) {
      return '';
    }

    switch (type) {
      case STATUS_TYPES.FAILED: {
        return "Down";
      }
      case STATUS_TYPES.ACTIVE: {
        return "Active";
      }
      case STATUS_TYPES.INACTIVE: {
        return "Inactive";
      }
      case STATUS_TYPES.SUCCESS: {
        return "Running";
      }
      case STATUS_TYPES.WARNING: {
        return "Under-replicated";
      }
      case STATUS_TYPES.PENDING: {
        return "Pending";
      }
      case STATUS_TYPES.IN_PROGRESS: {
        return "Bootstrapping";
      }
      case STATUS_TYPES.ERROR: {
        return "Error"
      }
      default: {
        return "Completed";
      }
    }
  };

  const getIcon = () => {
    switch (type) {
      case STATUS_TYPES.FAILED: {
        return <FailedIcon className={classes.colorFailed} />;
      }
      case STATUS_TYPES.ACTIVE: {
        return <FiberManualRecord className={classes.colorActive} />;
      }
      case STATUS_TYPES.INACTIVE: {
        return <FiberManualRecord className={classes.colorInactive} />;
      }
      case STATUS_TYPES.SUCCESS: {
        return <SuccessIcon className={classes.colorSuccess} />;
      }
      case STATUS_TYPES.WARNING: {
        return <WarningIcon className={classes.colorWarning} />;
      }
      case STATUS_TYPES.PENDING: {
        return <FiberManualRecord className={classes.colorPending} />;
      }
      case STATUS_TYPES.IN_PROGRESS: {
        return <LoadingIcon className={classes.loadingIcon} />;
      }
      case STATUS_TYPES.ERROR: {
        return <ErrorIcon className={classes.colorError} />
      }
      default: {
        return <CompletedIcon className={classes.colorCompleted} />;
      }
    }
  };

  if (value === 0) {
    return null;
  }

  return (
    <Box display="flex" alignItems="center" justifyContent="center">
      <YBTooltip  title={getTooltip()} children={<div style={{ lineHeight: 0.2 }}>{getIcon()}</div>}></YBTooltip>
      {label && <Box minWidth={35}>{label}</Box>}
    </Box>
  );
};

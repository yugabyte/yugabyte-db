import { makeStyles } from '@material-ui/core';
import clsx from 'clsx';

import { DrConfig, DrConfigState } from '../disasterRecovery/dtos';
import { ReactComponent as RightArrowWithBorder } from '../../../redesign/assets/arrow-right.svg';
import { assertUnreachableCase } from '../../../utils/errorHandlingUtils';

interface ReplicationIconProps {
  drConfig: DrConfig;
}

const useStyles = makeStyles((theme) => ({
  iconBorder: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',

    height: '28px',
    width: '28px',

    border: `1px solid ${theme.palette.grey[300]}`,
    borderRadius: '50%'
  },
  icon: {
    fontSize: '18px'
  },
  inProgress: {
    color: theme.palette.ybacolors.textInProgress
  },
  warning: {
    color: theme.palette.ybacolors.warning
  }
}));

export const ReplicationIcon = ({ drConfig }: ReplicationIconProps) => {
  const classes = useStyles();
  switch (drConfig.state) {
    case DrConfigState.INITIALIZING:
    case DrConfigState.SWITCHOVER_IN_PROGRESS:
    case DrConfigState.FAILOVER_IN_PROGRESS:
      return (
        <div className={classes.iconBorder}>
          <i className={clsx('fa fa-spinner fa-spin', classes.icon, classes.inProgress)} />
        </div>
      );
    case DrConfigState.REPLICATING:
      return <RightArrowWithBorder />;
    case DrConfigState.HALTED:
      return (
        <div className={classes.iconBorder}>
          <i className={clsx('fa fa-pause-circle-o', classes.icon, classes.warning)} />
        </div>
      );
    default:
      return assertUnreachableCase(drConfig.state);
  }
};

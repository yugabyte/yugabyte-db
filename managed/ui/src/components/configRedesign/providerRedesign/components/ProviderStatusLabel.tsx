import { makeStyles, Typography } from '@material-ui/core';
import clsx from 'clsx';
import { Variant } from '@material-ui/core/styles/createTypography';

import { assertUnreachableCase } from '../../../../utils/errorHandlingUtils';
import { ProviderStatus, ProviderStatusLabel as ProviderStatusTextLabel } from '../constants';

interface ProviderStatusLabelProps {
  providerStatus: ProviderStatus;

  variant?: Variant | 'inherit';
}

const useStyles = makeStyles((theme) => ({
  label: {
    display: 'flex',
    alignItems: 'center',

    '& i': {
      marginRight: theme.spacing(0.5)
    }
  },
  ready: {
    color: '#289b42'
  },
  inProgress: {
    color: '#44518b'
  },
  error: {
    color: '#e8473f'
  },
  deleting: {
    color: '#FF6347'
  }
}));

export const ProviderStatusLabel = ({
  providerStatus,
  variant = 'body2'
}: ProviderStatusLabelProps) => {
  const classes = useStyles();

  switch (providerStatus) {
    case ProviderStatus.READY:
      return (
        <Typography variant={variant}>
          <span className={clsx(classes.label, classes.ready)}>
            <i className="fa fa-check-circle" />
            {ProviderStatusTextLabel[providerStatus]}
          </span>
        </Typography>
      );
    case ProviderStatus.UPDATING:
      return (
        <Typography variant={variant}>
          <span className={clsx(classes.label, classes.inProgress)}>
            <i className="fa fa-spinner fa-spin" />
            {ProviderStatusTextLabel[providerStatus]}
          </span>
        </Typography>
      );
    case ProviderStatus.ERROR:
      return (
        <Typography variant={variant}>
          <span className={clsx(classes.label, classes.error)}>
            <i className="fa fa-close" />
            {ProviderStatusTextLabel[providerStatus]}
          </span>
        </Typography>
      );
      case ProviderStatus.DELETING:
      return (
        <Typography variant={variant}>
          <span className={clsx(classes.label, classes.deleting)}>
          <i className="fa fa-spinner fa-spin" />
            {ProviderStatusTextLabel[providerStatus]}
          </span>
        </Typography>
      );
    default:
      return assertUnreachableCase(providerStatus);
  }
};

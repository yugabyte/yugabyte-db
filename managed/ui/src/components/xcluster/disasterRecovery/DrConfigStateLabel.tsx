import clsx from 'clsx';
import { Variant } from '@material-ui/core/styles/createTypography';
import { makeStyles, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';

import { ReactComponent as InfoIcon } from '../../../redesign/assets/info-message.svg';
import { YBTooltip } from '../../../redesign/components';
import { assertUnreachableCase } from '../../../utils/errorHandlingUtils';

import { DrConfig, DrConfigState } from './dtos';

interface DrConfigStateLabelProps {
  drConfig: DrConfig;

  variant?: Variant | 'inherit';
}

const useStyles = makeStyles((theme) => ({
  label: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(0.5)
  },
  ready: {
    color: theme.palette.ybacolors.success
  },
  inProgress: {
    color: theme.palette.ybacolors.textInProgress
  },
  warning: {
    color: theme.palette.ybacolors.warning
  },
  infoIcon: {
    '&:hover': {
      cursor: 'pointer'
    }
  }
}));

const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.config.status';

export const DrConfigStateLabel = ({ drConfig, variant = 'body2' }: DrConfigStateLabelProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();

  switch (drConfig.state) {
    case DrConfigState.INITIALIZING:
    case DrConfigState.SWITCHOVER_IN_PROGRESS:
    case DrConfigState.FAILOVER_IN_PROGRESS:
      return (
        <Typography
          variant={variant}
          component="span"
          className={clsx(classes.label, classes.inProgress)}
        >
          <i className="fa fa-spinner fa-spin" />
          {t(drConfig.state)}
        </Typography>
      );
    case DrConfigState.REPLICATING:
      return (
        <Typography
          variant={variant}
          component="span"
          className={clsx(classes.label, classes.ready)}
        >
          <i className="fa fa-check-circle" />
          {t(drConfig.state)}
        </Typography>
      );
    case DrConfigState.HALTED:
      return (
        <Typography variant={variant} component="span" className={clsx(classes.label)}>
          <i className={clsx('fa fa-pause-circle-o', classes.warning)} />
          {t(`${drConfig.state}.label`)}
          <YBTooltip
            title={<Typography variant="body2">{t(`${drConfig.state}.tooltip`)}</Typography>}
          >
            <InfoIcon className={classes.infoIcon} />
          </YBTooltip>
        </Typography>
      );
    default:
      return assertUnreachableCase(drConfig.state);
  }
};

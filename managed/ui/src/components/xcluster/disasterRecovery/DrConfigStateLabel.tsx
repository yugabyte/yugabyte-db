import clsx from 'clsx';
import { Variant } from '@material-ui/core/styles/createTypography';
import { makeStyles, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';

import InfoIcon from '../../../redesign/assets/info-message.svg?img';
import { YBTooltip } from '../../../redesign/components';
import { assertUnreachableCase } from '../../../utils/errorHandlingUtils';
import { getTableCountsOfConcern } from '../ReplicationUtils';
import { I18N_ACCESSABILITY_ALT_TEXT_KEY_PREFIX } from '../../../redesign/helpers/constants';

import { DrConfig, DrConfigState } from './dtos';
import { usePillStyles } from '../../../redesign/styles/styles';

interface DrConfigStateLabelProps {
  drConfig: DrConfig;

  variant?: Variant | 'inherit';
}

const useStyles = makeStyles((theme) => ({
  stateLabelContainer: {
    display: 'flex',
    gap: theme.spacing(2),
    alignItems: 'center'
  },
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
  error: {
    color: theme.palette.ybacolors.error
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
  const pillClasses = usePillStyles();

  let stateLabel = null;
  switch (drConfig.state) {
    case DrConfigState.INITIALIZING:
    case DrConfigState.SWITCHOVER_IN_PROGRESS:
    case DrConfigState.FAILOVER_IN_PROGRESS:
    case DrConfigState.UPDATING:
      stateLabel = (
        <Typography
          variant={variant}
          component="span"
          className={clsx(classes.label, classes.inProgress)}
        >
          <i className="fa fa-spinner fa-spin" />
          {t(drConfig.state)}
        </Typography>
      );
      break;
    case DrConfigState.REPLICATING:
      stateLabel = (
        <Typography
          variant={variant}
          component="span"
          className={clsx(classes.label, classes.ready)}
        >
          <i className="fa fa-check-circle" />
          {t(drConfig.state)}
        </Typography>
      );
      break;
    case DrConfigState.HALTED:
      stateLabel = (
        <Typography variant={variant} component="span" className={clsx(classes.label)}>
          <i className={clsx('fa fa-pause-circle-o', classes.warning)} />
          {t(`${drConfig.state}.label`)}
          <YBTooltip
            title={<Typography variant="body2">{t(`${drConfig.state}.tooltip`)}</Typography>}
          >
            <img
              src={InfoIcon}
              alt={t('infoIcon', { keyPrefix: I18N_ACCESSABILITY_ALT_TEXT_KEY_PREFIX })}
            />
          </YBTooltip>
        </Typography>
      );
      break;
    case DrConfigState.FAILED:
      stateLabel = (
        <Typography
          variant={variant}
          component="span"
          className={clsx(classes.label, classes.error)}
        >
          <i className="fa fa-exclamation-triangle" />
          {t(drConfig.state)}
        </Typography>
      );
      break;
    default:
      return assertUnreachableCase(drConfig.state);
  }

  const tableCountsOfConcern = getTableCountsOfConcern(drConfig.tableDetails);
  return (
    <div className={classes.stateLabelContainer}>
      {stateLabel}
      {tableCountsOfConcern.uniqueTableCount > 0 && (
        <Typography variant="body2" className={clsx(pillClasses.pill, pillClasses.danger)}>
          {t('tablesOfConcernExist', { keyPrefix: 'clusterDetail.xCluster.shared' })}
        </Typography>
      )}
    </div>
  );
};

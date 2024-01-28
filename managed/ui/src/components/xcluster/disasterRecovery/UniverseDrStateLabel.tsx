import clsx from 'clsx';
import { Variant } from '@material-ui/core/styles/createTypography';
import { Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';

import { assertUnreachableCase } from '../../../utils/errorHandlingUtils';
import { UniverseXClusterRole } from '../constants';

import { SourceUniverseDrState, TargetUniverseDrState } from './dtos';
import { XClusterConfig } from '../dtos';
import { usePillStyles } from '../../../redesign/styles/styles';

interface UniverseDrStateLabelProps {
  xClusterConfig: XClusterConfig;
  universeXClusterRole: UniverseXClusterRole;

  variant?: Variant | 'inherit';
}

/**
 * State label for a universe involved in a DR config.
 */
export const UniverseDrStateLabel = ({
  xClusterConfig,
  universeXClusterRole,
  variant = 'body2'
}: UniverseDrStateLabelProps) => {
  const isSourceUniverse = universeXClusterRole === UniverseXClusterRole.SOURCE;
  const translationKeyPrefix = `clusterDetail.disasterRecovery.config.${
    isSourceUniverse ? 'sourceUniverseDrStatus' : 'targetUniverseDrStatus'
  }`;

  const { t } = useTranslation('translation', { keyPrefix: translationKeyPrefix });
  const classes = usePillStyles();

  const { sourceUniverseState, targetUniverseState } = xClusterConfig;

  if (!sourceUniverseState || !targetUniverseState) {
    return (
      <Typography
        variant={variant}
        component="span"
        className={clsx(classes.pill, classes.inactive)}
      >
        {t('undefined')}
      </Typography>
    );
  }

  const universeDrState = isSourceUniverse ? sourceUniverseState : targetUniverseState;

  switch (universeDrState) {
    case SourceUniverseDrState.UNCONFIGURED:
    case TargetUniverseDrState.UNCONFIGURED:
      return (
        <Typography
          variant={variant}
          component="span"
          className={clsx(classes.pill, classes.inactive)}
        >
          {t(universeDrState)}
        </Typography>
      );
    case SourceUniverseDrState.READY_TO_REPLICATE:
    case SourceUniverseDrState.WAITING_FOR_DR:
    case SourceUniverseDrState.PREPARING_SWITCHOVER:
    case SourceUniverseDrState.SWITCHING_TO_DR_REPLICA:
    case TargetUniverseDrState.BOOTSTRAPPING:
    case TargetUniverseDrState.SWITCHING_TO_DR_PRIMARY:
      return (
        <Typography
          variant={variant}
          component="span"
          className={clsx(classes.pill, classes.inProgress)}
        >
          {t(universeDrState)}
          <i className={clsx('fa fa-spinner fa-spin', classes.icon)} />
        </Typography>
      );
    case SourceUniverseDrState.REPLICATING_DATA:
    case TargetUniverseDrState.RECEIVING_DATA:
      return (
        <Typography
          variant={variant}
          component="span"
          className={clsx(classes.pill, classes.ready)}
        >
          {t(universeDrState)}
          <i className={clsx('fa fa-check', classes.icon)} />
        </Typography>
      );
    case SourceUniverseDrState.DR_FAILED:
    case TargetUniverseDrState.DR_FAILED:
      return (
        <Typography
          variant={variant}
          component="span"
          className={clsx(classes.pill, classes.danger)}
        >
          {t(universeDrState)}
          <i className={clsx('fa fa-check-circle', classes.icon)} />
        </Typography>
      );
    default:
      return assertUnreachableCase(universeDrState);
  }
};

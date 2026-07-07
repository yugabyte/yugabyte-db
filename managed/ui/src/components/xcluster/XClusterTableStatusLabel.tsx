import clsx from 'clsx';
import { Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { YBTooltip } from '../../redesign/components';

import { I18N_KEY_PREFIX_XCLUSTER_TERMS, XClusterTableStatus } from './constants';
import { XClusterReplicationStatusError } from './dtos';
import { assertUnreachableCase } from '../../utils/errorHandlingUtils';

import { usePillStyles } from '../../redesign/styles/styles';
import { makeStyles } from '@material-ui/core';

interface XClusterTableStatusProps {
  status: XClusterTableStatus;
  errors: string[];
  isDrInterface: boolean;
}

const useSelectStyles = makeStyles((theme) => ({
  pillContainer: {
    marginTop: theme.spacing(0.5)
  }
}));

const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.config.tableStatus';

// Maps a replication status error (the human readable string returned by the backend) to the
// i18n key of its label and tooltip under `${TRANSLATION_KEY_PREFIX}.replicationStatusError`.
const REPLICATION_STATUS_ERROR_KEY: Record<string, string> = {
  [XClusterReplicationStatusError.UNKNOWN_ERROR]: 'unknownError',
  [XClusterReplicationStatusError.MISSING_OP]: 'missingOpId',
  [XClusterReplicationStatusError.SCHEMA_MISMATCH]: 'schemaMismatch',
  [XClusterReplicationStatusError.MISSING_TABLE]: 'missingTable',
  [XClusterReplicationStatusError.ERROR_UNINITIALIZED]: 'uninitialized',
  [XClusterReplicationStatusError.AUTO_FLAG_CONFIG_MISMATCH]: 'autoFlagConfigMismatch',
  [XClusterReplicationStatusError.SOURCE_UNREACHABLE]: 'sourceUnreachable',
  [XClusterReplicationStatusError.SYSTEM_ERROR]: 'systemError'
};
export const XClusterTableStatusLabel = ({
  status,
  errors,
  isDrInterface
}: XClusterTableStatusProps) => {
  const classes = usePillStyles();
  const selectClasses = useSelectStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const tooltipInterpolationValues = {
    sourceUniverseTerm: t(`source.${isDrInterface ? 'dr' : 'xClusterReplication'}`, {
      keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
    }),
    targetUniverseTerm: t(`target.${isDrInterface ? 'dr' : 'xClusterReplication'}`, {
      keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
    }),
    xClusterOffering: t(`offering.${isDrInterface ? 'dr' : 'xClusterReplication'}`, {
      keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
    })
  };

  const renderStatusPill = (pillClassName: string, iconClassName: string) => (
    <YBTooltip
      title={
        <Typography variant="body2">
          {t(`${status}.tooltip`, tooltipInterpolationValues)}
        </Typography>
      }
    >
      <Typography variant="body2" className={clsx(classes.pill, pillClassName)}>
        {t(`${status}.label`)}
        <i className={iconClassName} />
      </Typography>
    </YBTooltip>
  );

  switch (status) {
    case XClusterTableStatus.RUNNING:
      return renderStatusPill(classes.ready, 'fa fa-check');
    case XClusterTableStatus.WARNING:
      return renderStatusPill(classes.warning, 'fa fa-exclamation-triangle');
    case XClusterTableStatus.FAILED:
      return renderStatusPill(classes.danger, 'fa fa-exclamation-circle');
    case XClusterTableStatus.ERROR:
      // When the table is in error status but no specific replication status errors are reported,
      // fall back to the generic `Error` label and tooltip.
      if (errors.length === 0) {
        return renderStatusPill(classes.danger, 'fa fa-exclamation-circle');
      }
      return (
        <span>
          {errors.map((error, index) => {
            const errorKey = REPLICATION_STATUS_ERROR_KEY[error];
            const errorPill = (
              <Typography
                variant="body2"
                className={clsx(classes.pill, classes.danger, selectClasses.pillContainer)}
              >
                {errorKey ? t(`replicationStatusError.${errorKey}.label`) : error}
                <i className="fa fa-exclamation-circle" />
              </Typography>
            );
            return errorKey ? (
              <YBTooltip
                key={`${error}-${index}`}
                title={
                  <Typography variant="body2">
                    {t(`replicationStatusError.${errorKey}.tooltip`, tooltipInterpolationValues)}
                  </Typography>
                }
              >
                {errorPill}
              </YBTooltip>
            ) : (
              <span key={`${error}-${index}`}>{errorPill}</span>
            );
          })}
        </span>
      );
    case XClusterTableStatus.UPDATING:
    case XClusterTableStatus.VALIDATED:
    case XClusterTableStatus.BOOTSTRAPPING:
      return renderStatusPill(classes.inProgress, 'fa fa-spinner fa-spin');
    case XClusterTableStatus.UNABLE_TO_FETCH:
      return renderStatusPill(classes.warning, 'fa fa-exclamation-triangle');
    case XClusterTableStatus.DROPPED_FROM_SOURCE:
    case XClusterTableStatus.DROPPED_FROM_TARGET:
    case XClusterTableStatus.EXTRA_TABLE_ON_TARGET:
    case XClusterTableStatus.EXTRA_TABLE_ON_SOURCE:
    case XClusterTableStatus.TABLE_INFO_MISSING:
    case XClusterTableStatus.DROPPED:
      return renderStatusPill(classes.danger, 'fa fa-exclamation-circle');
    default:
      return assertUnreachableCase(status);
  }
};

import clsx from 'clsx';
import { Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';

import { XClusterTableStatus } from './constants';
import { assertUnreachableCase } from '../../utils/errorHandlingUtils';

import { usePillStyles } from '../../redesign/styles/styles';

interface XClusterTableStatusProps {
  status: XClusterTableStatus;
}
const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.config.tableStatus';
export const XClusterTableStatusLabel = ({ status }: XClusterTableStatusProps) => {
  const classes = usePillStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  switch (status) {
    case XClusterTableStatus.RUNNING:
      return (
        <Typography variant="body2" className={clsx(classes.pill, classes.ready)}>
          {t(status)}
          <i className="fa fa-check" />
        </Typography>
      );
    case XClusterTableStatus.WARNING:
      return (
        <Typography variant="body2" className={clsx(classes.pill, classes.warning)}>
          {t(status)}
          <i className="fa fa-exclamation-triangle" />
        </Typography>
      );
    case XClusterTableStatus.FAILED:
      return (
        <Typography variant="body2" className={clsx(classes.pill, classes.danger)}>
          {t(status)}
          <i className="fa fa-exclamation-circle" />
        </Typography>
      );
    case XClusterTableStatus.ERROR:
      return (
        <Typography variant="body2" className={clsx(classes.pill, classes.danger)}>
          {t(status)}

          <i className="fa fa-exclamation-circle" />
        </Typography>
      );
    case XClusterTableStatus.UPDATING:
      return (
        <Typography variant="body2" className={clsx(classes.pill, classes.inProgress)}>
          {t(status)}
          <i className="fa fa-spinner fa-spin" />
        </Typography>
      );
    case XClusterTableStatus.VALIDATED:
      return (
        <Typography variant="body2" className={clsx(classes.pill, classes.inProgress)}>
          {t(status)}
          <i className="fa fa-spinner fa-spin" />
        </Typography>
      );
    case XClusterTableStatus.BOOTSTRAPPING:
      return (
        <Typography variant="body2" className={clsx(classes.pill, classes.inProgress)}>
          {t(status)}
          <i className="fa fa-spinner fa-spin" />
        </Typography>
      );
    case XClusterTableStatus.UNABLE_TO_FETCH:
      return (
        <Typography variant="body2" className={clsx(classes.pill, classes.warning)}>
          {t(status)}
          <i className="fa fa-exclamation-triangle" />
        </Typography>
      );
    case XClusterTableStatus.DROPPED:
      return (
        <Typography variant="body2" className={clsx(classes.pill, classes.danger)}>
          {t(status)}
          <i className="fa fa-exclamation-circle" />
        </Typography>
      );
    default:
      return assertUnreachableCase(status);
  }
};

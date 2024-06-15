import clsx from 'clsx';
import { Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';

import { XClusterTableStatus } from './constants';
import { assertUnreachableCase } from '../../utils/errorHandlingUtils';

import { usePillStyles } from '../../redesign/styles/styles';
import { makeStyles } from '@material-ui/core';

interface XClusterTableStatusProps {
  status: XClusterTableStatus;
  errors: string[];
}

const useSelectStyles = makeStyles((theme) => ({
  pillContainer: {
    marginTop: theme.spacing(0.5)
  }
}));

const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.config.tableStatus';
export const XClusterTableStatusLabel = ({ status, errors }: XClusterTableStatusProps) => {
  const classes = usePillStyles();
  const selectClasses = useSelectStyles();
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
        <span>
          {errors.map((error, i) => {
            return (
              <Typography
                variant="body2"
                className={clsx(classes.pill, classes.danger, selectClasses.pillContainer)}
              >
                {error}
                <i className="fa fa-exclamation-circle" />
              </Typography>
            );
          })}
        </span>
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

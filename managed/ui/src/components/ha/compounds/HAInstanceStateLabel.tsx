import { Typography } from '@material-ui/core';
import { Variant } from '@material-ui/core/styles/createTypography';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';
import { assertUnreachableCase } from '../../../utils/errorHandlingUtils';

import { HaPlatformInstance, HaInstanceState } from '../dtos';

import { usePillStyles } from '../../../redesign/styles/styles';

interface HAInstanceStatelabelProps {
  haInstance: HaPlatformInstance;

  variant?: Variant | 'inherit';
}

const TRANSLATION_KEY_PREFIX = 'ha.config.instanceState';

export const HAInstanceStatelabel = ({
  haInstance,
  variant = 'body2'
}: HAInstanceStatelabelProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = usePillStyles();

  switch (haInstance.instance_state) {
    case HaInstanceState.AWAITING_REPLICAS:
      return (
        <Typography
          variant={variant}
          component="span"
          className={clsx(classes.pill, classes.inProgress)}
        >
          <i className="fa fa-spinner fa-spin" />
          {t(haInstance.instance_state)}
        </Typography>
      );
    case HaInstanceState.CONNECTED:
      return (
        <Typography
          variant={variant}
          component="span"
          className={clsx(classes.pill, classes.ready)}
        >
          <i className="fa fa-check-circle" />
          {t(haInstance.instance_state)}
        </Typography>
      );
    case HaInstanceState.DISCONNECTED:
      return (
        <Typography
          variant={variant}
          component="span"
          className={clsx(classes.pill, classes.danger)}
        >
          <i className="fa fa-exclamation-triangle" />
          {t(haInstance.instance_state)}
        </Typography>
      );
    default:
      return assertUnreachableCase(haInstance.instance_state);
  }
};

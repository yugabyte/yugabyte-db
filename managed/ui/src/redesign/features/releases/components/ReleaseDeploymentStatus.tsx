import { useTranslation } from 'react-i18next';
import { Box, makeStyles } from '@material-ui/core';
import clsx from 'clsx';
import { getDeploymentStatus } from '../helpers/utils';
import { Releases, ReleaseState } from './dtos';

import Check from '../../../assets/check-new.svg';
import Revoke from '../../../assets/revoke.svg';

interface ReleaseDeploymentStatusProps {
  data: Releases | null;
}

const useStyles = makeStyles((theme) => ({
  smallerReleaseText: {
    fontWeight: 400,
    fontFamily: 'Inter',
    fontSize: '11.5px',
    color: theme.palette.grey[900],
    alignSelf: 'center'
  },
  deploymentBox: {
    borderRadius: '6px',
    padding: '4px 6px 4px 6px',
    maxWidth: 'fit-content',
    height: '24px'
  },
  verticalAlignText: {
    verticalAlign: 'super'
  },
  deploymentBoxGreen: {
    backgroundColor: theme.palette.success[100]
  },
  deploymentBoxGrey: {
    backgroundColor: theme.palette.grey[100]
  },
  deploymentGreenTag: {
    color: theme.palette.ybacolors.pillReadyIcon
  },
  deploymentGreyTag: {
    color: theme.palette.grey[700]
  },
  flexRow: {
    display: 'flex',
    flexDirection: 'row'
  },
  icon: {
    verticalAlign: 'top'
  }
}));

export const DeploymentStatus = ({ data }: ReleaseDeploymentStatusProps) => {
  const helperClasses = useStyles();
  const { t } = useTranslation();

  if (!data) {
    return <>{'--'}</>;
  }

  const deploymentStatus = getDeploymentStatus(data.state);
  const imgSrc = data.state === ReleaseState.ACTIVE ? Check : Revoke;
  return (
    <Box className={helperClasses.flexRow}>
      <Box
        className={clsx({
          [helperClasses.deploymentBox]: true,
          [helperClasses.deploymentBoxGreen]: data.state === ReleaseState.ACTIVE,
          [helperClasses.deploymentBoxGrey]:
            data.state === ReleaseState.DISABLED || data.state === ReleaseState.INCOMPLETE
        })}
      >
        <span
          title={t('releases.incompleteTooltipMessage')}
          data-testid={`DeploymentStatus-${deploymentStatus}`}
          className={clsx({
            [helperClasses.verticalAlignText]: true,
            [helperClasses.smallerReleaseText]: true,
            [helperClasses.deploymentGreenTag]: data.state === ReleaseState.ACTIVE,
            [helperClasses.deploymentGreyTag]:
              data.state === ReleaseState.DISABLED || data.state === ReleaseState.INCOMPLETE
          })}
        >
          {t(`releases.state.${deploymentStatus}`)}
        </span>
        <img src={imgSrc} alt="status" className={helperClasses.icon} />
      </Box>
    </Box>
  );
};

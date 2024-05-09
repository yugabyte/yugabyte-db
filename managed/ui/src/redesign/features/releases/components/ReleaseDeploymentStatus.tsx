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
    gap: '2px',
    maxWidth: 'fit-content',
    height: '28px'
  },
  deploymentBoxGreen: {
    backgroundColor: theme.palette.success[100]
  },
  deploymentBoxGrey: {
    backgroundColor: theme.palette.grey[200]
  },
  deploymentGreenTag: {
    color: theme.palette.success[700]
  },
  deploymentGreyTag: {
    color: theme.palette.grey[700]
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
    <Box
      className={clsx({
        [helperClasses.deploymentBox]: true,
        [helperClasses.deploymentBoxGreen]: data.state === ReleaseState.ACTIVE,
        [helperClasses.deploymentBoxGrey]: data.state === ReleaseState.DISABLED
      })}
    >
      <span
        data-testid={`DeploymentStatus-${deploymentStatus}`}
        className={clsx({
          [helperClasses.smallerReleaseText]: true,
          [helperClasses.deploymentGreenTag]: data.state === ReleaseState.ACTIVE,
          [helperClasses.deploymentGreyTag]: data.state === ReleaseState.DISABLED
        })}
      >
        {t(`releases.state.${deploymentStatus}`)}
      </span>
      <img src={imgSrc} alt="status" />
    </Box>
  );
};

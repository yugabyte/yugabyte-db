import { makeStyles, Typography } from '@material-ui/core';
import { SyncProblem } from '@material-ui/icons';

import { YBBanner, YBBannerVariant } from '../../../../common/descriptors';
import { YBButton } from '../../../../../redesign/components';

interface VersionWarningBannerProps {
  onReset: () => void;
  dataTestIdPrefix?: string;
}

const useStyles = makeStyles((theme) => ({
  infoBanner: {
    marginBottom: theme.spacing(2)
  },
  infoContent: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(2),
    '& p': {
      maxWidth: '80%'
    }
  },
  infoBannerActionButton: {
    marginLeft: 'auto',
    height: 'fit-content',
    minHeight: '32px'
  }
}));

export const VersionWarningBanner = ({ onReset, dataTestIdPrefix }: VersionWarningBannerProps) => {
  const classes = useStyles();

  return (
    <YBBanner variant={YBBannerVariant.WARNING} className={classes.infoBanner}>
      <div className={classes.infoContent}>
        <Typography variant="body2">
          <b>Warning!</b> A newer version of this provider exists in the database. You must reset
          the form with the latest version of this provider before making any changes. Please note
          that this will overwrite any existing unsaved changes.
        </Typography>
        <YBButton
          className={classes.infoBannerActionButton}
          variant="secondary"
          onClick={onReset}
          startIcon={<SyncProblem fontSize="large" />}
          type="button"
          data-testid={`${dataTestIdPrefix ?? 'ProviderForm'}-ResetFormButton`}
        >
          Reset form
        </YBButton>
      </div>
    </YBBanner>
  );
};

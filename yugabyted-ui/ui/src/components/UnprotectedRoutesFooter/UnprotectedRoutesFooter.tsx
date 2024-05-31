import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, makeStyles, Typography, Link as MUILink } from '@material-ui/core';
import { EXTERNAL_LINKS } from '@app/helpers';
import SystemStatus from '@app/assets/system-status.svg';

const COPYRIGHT_YEAR = new Date().getFullYear();

const useStyles = makeStyles((theme) => ({
  container: {
    '& *': {
      display: 'inline-block'
    }
  },
  statusIcon: {
    margin: theme.spacing(0, 0.5),
    position: 'relative',
    top: theme.spacing(0.3)
  }
}));

export const UnprotectedRoutesFooter: FC = () => {
  const { t } = useTranslation();
  const classes = useStyles();
  return (
    <Box className={classes.container}>
      <Typography variant="body2" noWrap color="textSecondary">
        {t('copyright', { year: COPYRIGHT_YEAR })}
      </Typography>
      <SystemStatus className={classes.statusIcon} />
      <MUILink href={EXTERNAL_LINKS.YB_CLOUD_STATUS_PAGE} target="_blank">
        <Typography variant="body2" noWrap color="textSecondary">
          {t('common.systemStatus')}
        </Typography>
      </MUILink>
    </Box>
  );
};

import React, { FC } from 'react';
import { Paper, Box, makeStyles, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import LockIcon from '@app/assets/lock-locked.svg';

const useStyles = makeStyles((theme) => ({
  root: {
    backgroundColor: theme.palette.primary[100],
    border: `1px dashed ${theme.palette.primary[300]}`
  }
}));

export const NoAccess: FC = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  return (
    <Paper className={classes.root}>
      <Box mt={3} mb={3} textAlign={'center'}>
        <LockIcon />
        <Box mt={2}>
          <Typography variant="body1">{t('common.noAccess.line1')}</Typography>
        </Box>
        <Box mt={0.75}>
          <Typography variant="subtitle1">{t('common.noAccess.line2')}</Typography>
        </Box>
      </Box>
    </Paper>
  );
};

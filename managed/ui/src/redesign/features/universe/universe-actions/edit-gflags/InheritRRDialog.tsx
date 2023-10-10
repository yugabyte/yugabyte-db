import React, { FC, useState } from 'react';
import _ from 'lodash';
import { useTranslation } from 'react-i18next';
import { Box, Typography, makeStyles } from '@material-ui/core';
import { YBModal } from '../../../../components';
import ErrorIcon from '../../../../assets/error.svg';

interface ModalProps {
  onCancel: () => void;
  onSubmit: () => void;
  open: boolean;
}

export const useStyles = makeStyles((theme) => ({
  infoContainer: {
    display: 'flex',
    flexDirection: 'row',
    padding: theme.spacing(1.5, 2),
    height: '74px',
    background: '#FDE2E2',
    borderRadius: theme.spacing(1)
  }
}));

export const InheritRRDialog: FC<ModalProps> = ({ onCancel, onSubmit, open }) => {
  const classes = useStyles();
  const { t } = useTranslation();
  return (
    <YBModal
      open={open}
      titleSeparator
      size="sm"
      overrideHeight={300}
      overrideWidth={600}
      cancelLabel={t('common.cancel')}
      submitLabel={t('common.applyChanges')}
      title={t('common.areYouSure')}
      onClose={onCancel}
      onSubmit={onSubmit}
      submitTestId="InheritRRDialog-Submit"
      cancelTestId="InheritRRDialog-Close"
    >
      <Box
        height="100%"
        width="100%"
        display="flex"
        flexDirection="column"
        data-testid="InheritRRDialog-Modal"
      >
        <Box className={classes.infoContainer}>
          <Box display="flex" flexShrink={1} alignItems="flex-start">
            <img alt="" src={ErrorIcon} />
          </Box>
          <Box display="flex" flexDirection="column" ml={1}>
            <Typography variant="body2">
              {t('universeForm.gFlags.inheritRRModalLine1')}
              <br />
              {t('universeForm.gFlags.inheritRRModalLine2')}
              <br />
              {t('universeForm.gFlags.inheritRRModalLine3')}
            </Typography>
          </Box>
        </Box>
        <Box mt={4}>
          <Typography variant="body2">
            {t('universeForm.gFlags.inheritRRModalConfirmMsg')}
          </Typography>
        </Box>
      </Box>
    </YBModal>
  );
};

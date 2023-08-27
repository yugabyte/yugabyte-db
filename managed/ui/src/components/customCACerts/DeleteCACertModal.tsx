/*
 * Created on Wed Jun 07 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, Grid, Typography, makeStyles } from '@material-ui/core';
import { useMutation, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import { CACert } from './ICerts';
import { deleteCertificate } from './CACertsApi';
import { YBModal } from '../../redesign/components';
import { ybFormatDate } from '../../redesign/helpers/DateUtils';
import { createErrorMessage } from '../../utils/ObjectUtils';

type DeleteCACertModalProps = {
  visible: boolean;
  deleteCADetails: CACert | null;
  onHide: () => void;
};

const useStyle = makeStyles((theme) => ({
  root: {
    padding: `${theme.spacing(1)}px ${theme.spacing(2)}px`,
    color: '#333333'
  },
  detailsPanel: {
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: theme.spacing(1),
    margin: 0,
    marginTop: theme.spacing(3),
    padding: theme.spacing(2.5),
    color: theme.palette.grey[900],
    '& .header': {
      color: theme.palette.grey[600],
      textTransform: 'uppercase'
    }
  },
  modalTitle: {
    marginLeft: theme.spacing(2.25)
  }
}));

const DeleteCACertModal: FC<DeleteCACertModalProps> = ({ visible, deleteCADetails, onHide }) => {
  const { t } = useTranslation();
  const classes = useStyle();
  const queryClient = useQueryClient();

  const deleteCert = useMutation(() => deleteCertificate(deleteCADetails!.id), {
    onSuccess: () => {
      toast.success(t('customCACerts.deleteCACertModal.successMsg'));
      queryClient.invalidateQueries('ca_certs_list');
      onHide();
    },
    onError: (resp) => {
      toast.error(createErrorMessage(resp));
    }
  });

  if (!visible || deleteCADetails === null) return null;

  return (
    <YBModal
      open={visible}
      title={t('customCACerts.deleteCACertModal.title')}
      onClose={onHide}
      titleSeparator
      cancelLabel={t('common.cancel')}
      submitLabel={t('customCACerts.deleteCACertModal.title')}
      overrideWidth="560px"
      overrideHeight="400px"
      size="md"
      onSubmit={() => {
        deleteCert.mutate();
      }}
      titleContentProps={classes.modalTitle}
    >
      <Box className={classes.root}>
        <Typography variant="body2">{t('customCACerts.deleteCACertModal.info')}</Typography>
        <Grid container className={classes.detailsPanel} spacing={2}>
          <Grid item xs={5} className="header">
            {t('customCACerts.listing.table.name')}
          </Grid>
          <Grid item xs={7}>
            {deleteCADetails?.name}
          </Grid>
          <Grid item xs={5} className="header">
            {t('customCACerts.listing.table.validFrom')}
          </Grid>
          <Grid item xs={7}>
            {ybFormatDate(deleteCADetails!.startDate)}
          </Grid>
          <Grid item xs={5} className="header">
            {t('customCACerts.listing.table.validUntil')}
          </Grid>
          <Grid item xs={7}>
            {ybFormatDate(deleteCADetails!.expiryDate)}
          </Grid>
        </Grid>
      </Box>
    </YBModal>
  );
};

export default DeleteCACertModal;

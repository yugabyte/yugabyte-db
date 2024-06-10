/*
 * Created on Mon Apr 10 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, Typography, makeStyles } from '@material-ui/core';
import { YBModal } from '../../../../../components';

type RollbackToTemplateModalProps = {
  visible: boolean;
  onHide: () => void;
  onSubmit: () => void;
};

const useStyles = makeStyles((theme) => ({
  root: {
    padding: theme.spacing(2.75)
  }
}));

const RollbackToTemplateModal: FC<RollbackToTemplateModalProps> = ({
  visible,
  onHide,
  onSubmit
}) => {
  const { t } = useTranslation();
  const classes = useStyles();
  return (
    <YBModal
      open={visible}
      title={t('alertCustomTemplates.rollbackModal.title')}
      overrideWidth="560px"
      overrideHeight="260px"
      titleSeparator
      onClose={onHide}
      cancelLabel={t('common.close')}
      submitLabel={t('common.apply')}
      onSubmit={onSubmit}
      enableBackdropDismiss
    >
      <Box className={classes.root}>
        <Typography variant="body2">{t('alertCustomTemplates.rollbackModal.helpText')}</Typography>
      </Box>
    </YBModal>
  );
};

export default RollbackToTemplateModal;

/*
 * Created on Fri Oct 06 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { makeStyles } from '@material-ui/core';
import { YBModal } from '../../../../components';
import ErrorIcon from '../../../../assets/error.svg';

type ConfirmEditRoleModalProps = {
  open: boolean;
  onHide: () => void;
  onSubmit: () => void;
};

const useStyles = makeStyles((theme) => ({
  root: {
    padding: theme.spacing(2)
  },
  errMsg: {
    display: 'flex',
    alignItems: 'flex-start',
    padding: theme.spacing(2),
    borderRadius: theme.spacing(1),
    background: '#fce2e1',
    marginBottom: theme.spacing(3),
    gap: theme.spacing(1)
  }
}));

export const ConfirmEditRoleModal: FC<ConfirmEditRoleModalProps> = ({ open, onHide, onSubmit }) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.roles.edit.confirmModal'
  });

  const classes = useStyles();

  return (
    <YBModal
      open={open}
      title={t('title')}
      dialogContentProps={{
        className: classes.root
      }}
      submitLabel={t('applyChanges', { keyPrefix: 'common' })}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      onSubmit={() => {
        onSubmit();
      }}
      overrideHeight={'305px'}
      overrideWidth={'600px'}
      onClose={onHide}
    >
      <div className={classes.errMsg}>
        <img src={ErrorIcon} alt="error" />
        <div>{t('errMsg')}</div>
      </div>
      <div>{t('editRoleConfirm')}</div>
    </YBModal>
  );
};

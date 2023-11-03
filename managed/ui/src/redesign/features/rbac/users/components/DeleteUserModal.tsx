/*
 * Created on Tue Jul 18 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useContext, useEffect } from 'react';
import { useForm } from 'react-hook-form';
import { useMutation, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import { Trans, useTranslation } from 'react-i18next';
import { makeStyles } from '@material-ui/core';
import { YBInputField, YBModal } from '../../../../components';
import { deleteUser } from '../../api';
import { createErrorMessage } from '../../../universe/universe-form/utils/helpers';
import ErrorIcon from '../../../../assets/error.svg';
import { RbacUser } from '../interface/Users';
import { UserContextMethods, UserPages, UserViewContext } from './UserContext';

type DeleteUserProps = {
  open: boolean;
  onHide: () => void;
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

type DeleteUserFormProps = {
  userEmail: RbacUser['email'];
};

export const DeleteUserModal: FC<DeleteUserProps> = ({ open, onHide }) => {
  const [{ currentUser }, { setCurrentPage }] = (useContext(
    UserViewContext
  ) as unknown) as UserContextMethods;

  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.users.delete'
  });

  const queryClient = useQueryClient();

  const doDeleteUser = useMutation(() => deleteUser(currentUser!), {
    onSuccess: () => {
      toast.success(t('successMsg', { user_email: currentUser?.email }));
      setCurrentPage(UserPages.LIST_USER);
      queryClient.invalidateQueries('users');
      onHide();
    },
    onError: (err) => {
      toast.error(createErrorMessage(err));
      onHide();
    }
  });

  const classes = useStyles();

  const {
    control,
    formState: { isValid },
    reset
  } = useForm<DeleteUserFormProps>();

  useEffect(() => {
    if (open) {
      reset();
    }
  }, [open]);

  return (
    <YBModal
      open={open}
      title={t('title')}
      buttonProps={{
        primary: {
          disabled: !isValid
        }
      }}
      dialogContentProps={{
        className: classes.root
      }}
      submitLabel={t('title')}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      onSubmit={() => {
        doDeleteUser.mutate();
      }}
      overrideHeight={'360px'}
      overrideWidth={'600px'}
      onClose={onHide}
    >
      <div className={classes.errMsg}>
        <img src={ErrorIcon} alt="error" />
        <div>
          <Trans
            i18nKey="rbac.users.delete.errMsg"
            values={{ user_email: currentUser?.email }}
            components={{ br: <br />, bold: <b /> }}
          />
        </div>
      </div>
      <YBInputField
        control={control}
        rules={{
          required: 'Required',
          validate: (e) => e === currentUser?.email
        }}
        name="userEmail"
        label={t('form.email', { keyPrefix: 'rbac.users.create' })}
        placeholder={t('form.email', { keyPrefix: 'rbac.users.create' })}
        fullWidth
      />
    </YBModal>
  );
};

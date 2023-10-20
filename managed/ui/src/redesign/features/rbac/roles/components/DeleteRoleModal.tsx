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
import { Pages, RoleContextMethods, RoleViewContext } from '../RoleContext';
import { deleteRole } from '../../api';
import { createErrorMessage } from '../../../universe/universe-form/utils/helpers';
import { Role } from '../IRoles';
import ErrorIcon from '../../../../assets/error.svg';

type DeleteRoleProps = {
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

type DeleteRoleFormProps = {
  roleName: Role['name'];
};

export const DeleteRoleModal: FC<DeleteRoleProps> = ({ open, onHide }) => {
  const [{ currentRole }, { setCurrentPage }] = (useContext(
    RoleViewContext
  ) as unknown) as RoleContextMethods;

  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.roles.delete'
  });

  const queryClient = useQueryClient();

  const doDeleteRole = useMutation(() => deleteRole(currentRole!), {
    onSuccess: () => {
      toast.success(t('successMsg', { role_name: currentRole?.name }));
      queryClient.invalidateQueries('roles');
      setCurrentPage(Pages.LIST_ROLE);
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
  } = useForm<DeleteRoleFormProps>();

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
        doDeleteRole.mutate();
      }}
      overrideHeight={'360px'}
      overrideWidth={'600px'}
      onClose={onHide}
    >
      <div className={classes.errMsg}>
        <img src={ErrorIcon} alt="error" />
        <div>
          <Trans
            i18nKey="rbac.roles.delete.errMsg"
            values={{ role_name: currentRole?.name }}
            components={{ br: <br />, bold: <b /> }}
          />
        </div>
      </div>
      <YBInputField
        control={control}
        rules={{
          required: 'Required',
          validate: (e) => e === currentRole?.name
        }}
        name="roleName"
        label={t('form.namePlaceholder', { keyPrefix: 'rbac.roles.create' })}
        placeholder={t('form.namePlaceholder', { keyPrefix: 'rbac.roles.create' })}
        fullWidth
      />
    </YBModal>
  );
};

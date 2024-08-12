/*
 * Created on Tue Jul 23 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useEffect } from 'react';
import { useForm } from 'react-hook-form';
import { useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import { Trans, useTranslation } from 'react-i18next';
import { makeStyles } from '@material-ui/core';
import { YBInputField, YBModal } from '../../../../components';
import { AuthGroupToRolesMapping } from '../../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';
import {
  getListMappingsQueryKey,
  useDeleteGroupMappings
} from '../../../../../v2/api/authentication/authentication';
import { GetGroupContext } from './GroupUtils';
import { Pages } from './GroupContext';
import ErrorIcon from '../../../../assets/error.svg';

type DeleteGroupProps = {
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
    gap: theme.spacing(1),
    wordBreak: 'break-all'
  },
  preserveSpaces: {
    whiteSpace: 'pre-wrap'
  }
}));

export const DeleteGroupModal: FC<DeleteGroupProps> = ({ open, onHide }) => {
  const [{ currentGroup }, { setCurrentGroup, setCurrentPage }] = GetGroupContext();

  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.groups.delete'
  });

  const queryClient = useQueryClient();
  const queryKey = getListMappingsQueryKey();

  const doDeleteRole = useDeleteGroupMappings({
    mutation: {
      onSuccess: () => {
        toast.success(t('successMsg', { group_name: currentGroup?.group_identifier }));
        queryClient.invalidateQueries(queryKey);
        onHide();
      }
    }
  });

  const classes = useStyles();

  const {
    control,
    formState: { isValid },
    reset
  } = useForm<AuthGroupToRolesMapping>();

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
        doDeleteRole
          .mutateAsync({
            gUUID: currentGroup!.uuid!
          })
          .then(() => {
            queryClient.invalidateQueries(queryKey);
            setCurrentGroup(null);
            setCurrentPage(Pages.LIST_GROUP);
            onHide();
          });
      }}
      overrideHeight={'360px'}
      overrideWidth={'600px'}
      onClose={onHide}
    >
      <div className={classes.errMsg}>
        <img src={ErrorIcon} alt="error" />
        <div>
          <Trans
            t={t}
            i18nKey="errMsg"
            values={{ group_name: currentGroup?.group_identifier }}
            components={{ br: <br />, bold: <b className={classes.preserveSpaces} /> }}
          />
        </div>
      </div>
      <YBInputField
        control={control}
        rules={{
          required: 'Required',
          validate: (e) => e === currentGroup?.group_identifier
        }}
        name="group_identifier"
        label={t('groupNamePlaceholder')}
        placeholder={t('groupName')}
        fullWidth
        data-testid="delete-group_identifier"
      />
    </YBModal>
  );
};

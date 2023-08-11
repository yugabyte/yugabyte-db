/*
 * Created on Wed Jul 12 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useContext, useImperativeHandle, useRef } from 'react';
import { useForm } from 'react-hook-form';
import { useMutation, useQuery } from 'react-query';
import { toast } from 'react-toastify';
import { isEmpty } from 'lodash';
import { useToggle } from 'react-use';
import { useTranslation } from 'react-i18next';
import { Box, Divider, Grid, Typography, makeStyles } from '@material-ui/core';
import Container from '../../common/Container';
import ListPermissionsModal from '../../permission/ListPermissionsModal';
import { YBButton, YBInputField } from '../../../../components';
import { YBLoadingCircleIcon } from '../../../../../components/common/indicators';
import { IRole } from '../IRoles';
import { Permission } from '../../permission';
import { RoleContextMethods, RoleViewContext } from '../RoleContext';
import { createRole, editRole, getAllAvailablePermissions } from '../../api';
import { getPermissionDisplayText } from '../../rbacUtils';
import { createErrorMessage } from '../../../universe/universe-form/utils/helpers';
import { Create } from '@material-ui/icons';

const useStyles = makeStyles((theme) => ({
  root: {
    padding: theme.spacing(4),
    width: '700px',
    height: '700px'
  },
  title: {
    fontSize: '17px',
    marginBottom: theme.spacing(3)
  },
  form: {
    '&>div': {
      marginBottom: theme.spacing(3)
    },
    '& .MuiInputLabel-root': {
      textTransform: 'unset',
      fontSize: '13px',
      marginBottom: theme.spacing(0.8),
      fontWeight: 400,
      color: '#333'
    }
  }
}));

export const CreateRoleWithContainer = () => {
  const createRoleRef = useRef<any>(null);

  return (
    <Container
      onCancel={() => {
        createRoleRef.current?.onCancel();
      }}
      onSave={() => {
        createRoleRef.current?.onSave();
      }}
    >
      <CreateRole ref={createRoleRef} />
    </Container>
  );
};
// eslint-disable-next-line react/display-name
export const CreateRole = forwardRef((_, forwardRef) => {
  const classes = useStyles();
  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.roles.create'
  });

  const [{ currentRole }, { setCurrentPage }] = (useContext(
    RoleViewContext
  ) as unknown) as RoleContextMethods;

  const { control, setValue, handleSubmit, watch } = useForm<IRole>({
    defaultValues: currentRole
      ? {
          ...currentRole,
          permissionDetails: currentRole.permissionDetails
        }
      : {
          permissionDetails: {
            permissionList: []
          }
        }
  });

  const doCreateRole = useMutation(
    (role: IRole) => {
      return createRole(role);
    },
    {
      onSuccess: (_resp, role) => {
        toast.success(t('successMsg', { role_name: role.name }));
        setCurrentPage('LIST_ROLE');
      },
      onError: (err) => {
        toast.error(createErrorMessage(err));
      }
    }
  );

  const doEditRole = useMutation(
    (role: IRole) => {
      return editRole(role);
    },
    {
      onSuccess: (_resp, role) => {
        toast.success(t('editSuccessMsg', { role_name: role.name }));
        setCurrentPage('LIST_ROLE');
      },
      onError: (err) => {
        toast.error(createErrorMessage(err));
      }
    }
  );

  const onSave = () => {
    handleSubmit((val) => {
      if (currentRole === null) {
        doCreateRole.mutate(val);
      } else {
        doEditRole.mutate(val);
      }
    })();
  };

  const onCancel = () => {
    setCurrentPage('LIST_ROLE');
  };

  useImperativeHandle(
    forwardRef,
    () => ({
      onSave,
      onCancel
    }),
    [onSave, onCancel]
  );

  return (
    <Container onSave={onSave} onCancel={onCancel}>
      <Box className={classes.root}>
        <div className={classes.title}>{t(currentRole ? 'edit' : 'title')}</div>
        <form className={classes.form}>
          <YBInputField
            name="name"
            control={control}
            label={t('form.name')}
            placeholder={t('form.namePlaceholder')}
            fullWidth
            disabled={currentRole !== null}
          />
          <YBInputField
            name="description"
            control={control}
            label={t('form.description')}
            placeholder={t('form.descriptionPlaceholder')}
            fullWidth
          />
          <SelectPermissions
            selectedPermissions={watch('permissionDetails.permissionList')}
            setSelectedPermissions={(perm: Permission[]) => {
              setValue('permissionDetails.permissionList', perm);
            }}
          />
        </form>
      </Box>
    </Container>
  );
});

const permissionsStyles = makeStyles((theme) => ({
  root: {
    width: '100%',
    borderRadius: theme.spacing(1),
    border: `1px dashed ${theme.palette.primary[300]}`,
    background: theme.palette.primary[100],
    height: '126px',
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2),
    alignItems: 'center',
    justifyContent: 'center'
  },
  helpText: {
    fontFamily: 'Inter',
    fontWeight: 400,
    lineHeight: `${theme.spacing(2)}px`,
    color: '#67666C'
  },
  permList: {
    width: '100%'
  },
  header: {
    height: '50px',
    borderRadius: `${theme.spacing(1)}px ${theme.spacing(1)}px 0px 0px`,
    border: '1px solid #E5E5E6',
    background: theme.palette.ybacolors.backgroundGrayLightest,
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    padding: theme.spacing(2)
  },
  selectionCount: {
    width: theme.spacing(5),
    height: theme.spacing(3),
    padding: theme.spacing(0.8),
    border: '1px solid #E5E5E6',
    background: theme.palette.common.white,
    display: 'flex',
    alignItems: 'center',
    borderRadius: theme.spacing(0.75)
  },
  divider: {
    marginLeft: theme.spacing(1),
    marginRight: theme.spacing(2)
  },
  editSelection: {
    display: 'flex',
    gap: theme.spacing(0.5),
    alignItems: 'center',
    cursor: 'pointer',
    userSelect: 'none',
    '& svg': {
      marginRight: theme.spacing(0.5)
    }
  },
  permItems: {
    border: '1px solid #E5E5E6',
    borderTop: 0,
    padding: theme.spacing(2),
    '& > div': {
      marginBottom: theme.spacing(2)
    }
  }
}));

type SelectPermissionsProps = {
  selectedPermissions: Permission[];
  setSelectedPermissions: (permissions: Permission[]) => void;
};

const SelectPermissions = ({
  selectedPermissions,
  setSelectedPermissions
}: SelectPermissionsProps) => {
  const classes = permissionsStyles();
  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.roles.create.form'
  });

  const [permissionModalVisible, togglePermissionModal] = useToggle(false);

  const { isLoading, data: availablePermissions } = useQuery(
    'permissions',
    () => getAllAvailablePermissions(),
    {
      select: (data) => data.data
    }
  );

  if (isLoading) return <YBLoadingCircleIcon />;

  const getEmptyList = () => (
    <Box className={classes.root}>
      <YBButton variant="secondary" onClick={() => togglePermissionModal(true)}>
        {t('selectPermissions')}
      </YBButton>
      <div className={classes.helpText}>{t('selectPermissionSubText')}</div>
    </Box>
  );

  const listPermissions = () => (
    <div className={classes.permList}>
      <div className={classes.header}>
        <span>{t('permissions')}</span>
        <Grid container alignItems="center" justifyContent="flex-end" spacing={1}>
          <div className={classes.selectionCount}>
            <Typography variant="subtitle1">{selectedPermissions.length}&nbsp;/&nbsp;</Typography>
            <Typography variant="subtitle1">{availablePermissions?.length}</Typography>
          </div>
          <Divider orientation="vertical" flexItem className={classes.divider} />
          <div className={classes.editSelection} onClick={() => togglePermissionModal(true)}>
            <Create />
            {t('editSelection')}
          </div>
        </Grid>
      </div>
      <div className={classes.permItems}>
        {selectedPermissions.map((p, i) => (
          <div key={i}>{getPermissionDisplayText(p)}</div>
        ))}
      </div>
    </div>
  );

  return (
    <>
      {isEmpty(selectedPermissions) ? getEmptyList() : listPermissions()}
      <ListPermissionsModal
        visible={permissionModalVisible}
        onHide={() => togglePermissionModal(false)}
        onSubmit={setSelectedPermissions}
        permissionsList={availablePermissions ?? []}
        defaultPerm={selectedPermissions}
      />
    </>
  );
};

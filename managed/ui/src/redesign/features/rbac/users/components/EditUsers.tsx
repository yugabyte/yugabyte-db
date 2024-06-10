/*
 * Created on Tue Aug 08 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { useContext } from 'react';
import { useTranslation } from 'react-i18next';
import { FormProvider, useForm } from 'react-hook-form';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { yupResolver } from '@hookform/resolvers/yup';
import { useToggle } from 'react-use';
import { toast } from 'react-toastify';
import { find } from 'lodash';

import { Box, FormHelperText, makeStyles } from '@material-ui/core';
import Container from '../../common/Container';
import { DeleteUserModal } from './DeleteUserModal';
import { YBLoadingCircleIcon } from '../../../../../components/common/indicators';
import { RolesAndResourceMapping } from '../../policy/RolesAndResourceMapping';
import { YBButton, YBInputField } from '../../../../components';
import { editUsersRolesBindings, getRoleBindingsForUser } from '../../api';
import { convertRbacBindingsToUISchema } from './UserUtils';
import { RbacValidator, hasNecessaryPerm } from '../../common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../ApiAndUserPermMapping';

import { RbacUserWithResources } from '../interface/Users';
import { UserContextMethods, UserPages, UserViewContext } from './UserContext';
import { createErrorMessage } from '../../../universe/universe-form/utils/helpers';
import { ForbiddenRoles, Role } from '../../roles';
import { getEditUserValidationSchema } from './UserValidationSchema';

import { ReactComponent as ArrowLeft } from '../../../../assets/arrow_left.svg';
import { ReactComponent as Delete } from '../../../../assets/trashbin.svg';

const useStyles = makeStyles((theme) => ({
  root: {
    padding: `0 ${theme.spacing(3)}px`,
    minHeight: '500px'
  },
  header: {
    borderBottom: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    height: '75px',
    display: 'flex',
    justifyContent: 'space-between',
    alignItems: 'center',
    fontSize: '20px',
    fontWeight: 700
  },
  back: {
    display: 'flex',
    alignItems: 'center',
    '& svg': {
      width: theme.spacing(4),
      height: theme.spacing(4),
      marginRight: theme.spacing(2),
      cursor: 'pointer'
    }
  },
  form: {
    width: '1032px',
    padding: theme.spacing(3),
    '& > div': {
      marginBottom: theme.spacing(3)
    }
  },
  email: {
    width: '500px'
  }
}));

export const EditUser = () => {
  const classes = useStyles();

  const [{ currentUser }, { setCurrentPage, setCurrentUser }] = (useContext(
    UserViewContext
  ) as unknown) as UserContextMethods;

  const { t } = useTranslation('translation', { keyPrefix: 'rbac.users.edit' });

  const methods = useForm<RbacUserWithResources>({
    defaultValues: currentUser ?? {},
    resolver: yupResolver(getEditUserValidationSchema(t))
  });

  const {
    formState: { errors, isValid }
  } = methods;

  const queryClient = useQueryClient();

  const [showDeleteModal, toggleDeleteModal] = useToggle(false);

  const editUser = useMutation(
    () => editUsersRolesBindings(currentUser!.uuid!, methods.getValues()),
    {
      onSuccess() {
        toast.success(t('successMsg', { user_email: currentUser!.email }));
        queryClient.invalidateQueries('users');
        setCurrentPage(UserPages.LIST_USER);
      },
      onError: (err) => {
        toast.error(createErrorMessage(err));
      }
    }
  );

  const { isLoading, data: roleBindings } = useQuery(
    ['role_binding', currentUser?.uuid],
    () => getRoleBindingsForUser(currentUser!.uuid!),
    {
      onSuccess(resp) {
        const userBindings = resp.data[currentUser!.uuid!];
        if (userBindings.length > 0) {
          const formValues = convertRbacBindingsToUISchema(userBindings);
          methods.reset(formValues);
        }
      }
    }
  );

  if (isLoading) return <YBLoadingCircleIcon />;

  let userRoles: Role[] = [];
  let isSuperAdmin = false;

  if (currentUser?.uuid) {
    userRoles = [...(roleBindings?.[currentUser.uuid] ?? [])].map((r) => r.role);
    isSuperAdmin = userRoles.some((role) =>
      find(ForbiddenRoles, { name: role.name, roleType: role.roleType })
    );
  }

  return (
    <Container
      onCancel={() => {
        setCurrentUser(null);
        setCurrentPage(UserPages.LIST_USER);
      }}
      onSave={() => {
        methods.handleSubmit(() => {
          editUser.mutate();
        })();
      }}
      saveLabel={t('title')}
      disableSave={
        currentUser?.uuid !== undefined &&
        !hasNecessaryPerm({
          ...ApiPermissionMap.MODIFY_USER,
          onResource: { USER: currentUser?.uuid }
        })
      }
    >
      <Box className={classes.root}>
        <div className={classes.header}>
          <div className={classes.back}>
            <ArrowLeft
              onClick={() => {
                setCurrentPage(UserPages.LIST_USER);
                setCurrentUser(null);
              }}
              data-testid={`rbac-resource-back-to-users`}
            />
            {t('title')}
          </div>
          <RbacValidator
            accessRequiredOn={{
              ...ApiPermissionMap.DELETE_USER,
              onResource: { USER: currentUser?.uuid }
            }}
            customValidateFunction={() => {
              return (
                hasNecessaryPerm({
                  ...ApiPermissionMap.DELETE_USER,
                  onResource: { USER: currentUser?.uuid }
                }) &&
                currentUser?.uuid !== localStorage.getItem('userId') &&
                !isSuperAdmin
              );
            }}
            isControl
          >
            <YBButton
              variant="secondary"
              size="large"
              startIcon={<Delete />}
              onClick={() => {
                toggleDeleteModal(true);
              }}
              data-testid={`rbac-resource-delete-user`}
            >
              {t('delete')}
            </YBButton>
          </RbacValidator>
        </div>
        <FormProvider {...methods}>
          <form className={classes.form}>
            <YBInputField
              name="email"
              control={methods.control}
              label={t('form.email', { keyPrefix: 'rbac.users.create' })}
              placeholder={t('form.emailPlaceholder', { keyPrefix: 'rbac.users.create' })}
              fullWidth
              disabled
              className={classes.email}
            />
            <RolesAndResourceMapping />
            {errors.roleResourceDefinitions?.message && (
              <FormHelperText required error>
                {errors.roleResourceDefinitions.message}
              </FormHelperText>
            )}
          </form>
        </FormProvider>
        <DeleteUserModal
          open={showDeleteModal}
          onHide={() => {
            toggleDeleteModal(false);
          }}
        />
      </Box>
    </Container>
  );
};

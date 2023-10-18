/*
 * Created on Mon Jul 31 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useContext, useImperativeHandle, useRef } from 'react';
import { useMutation, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import { yupResolver } from '@hookform/resolvers/yup';
import { useTranslation } from 'react-i18next';
import { FormProvider, useForm } from 'react-hook-form';
import { Box, FormHelperText, makeStyles } from '@material-ui/core';
import Container from '../../common/Container';
import { RolesAndResourceMapping } from '../../policy/RolesAndResourceMapping';
import { YBInputField, YBPasswordField } from '../../../../components';
import { createUser } from '../../api';
import { RbacUserWithResources } from '../interface/Users';
import { Resource } from '../../permission';
import { UserContextMethods, UserPages, UserViewContext } from './UserContext';
import { createErrorMessage } from '../../../universe/universe-form/utils/helpers';
import { getUserValidationSchema } from './UserValidationSchema';

const useStyles = makeStyles((theme) => ({
  root: {
    padding: theme.spacing(4),
    width: '700px',
    minHeight: '400px'
  },
  title: {
    fontSize: '17px',
    marginBottom: theme.spacing(3)
  },
  form: {
    '&>div': {
      marginBottom: theme.spacing(1.5)
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

export const initialMappingValue: RbacUserWithResources = {
  roleResourceDefinitions: [
    {
      roleType: 'System',
      role: null,
      resourceGroup: {
        resourceDefinitionSet: [
          {
            allowAll: true,
            resourceType: Resource.UNIVERSE,
            resourceUUIDSet: []
          }
        ]
      }
    }
  ]
};

const initialFormValues: RbacUserWithResources = {
  email: '',
  password: '',
  confirmPassword: '',
  roleResourceDefinitions: []
};

// eslint-disable-next-line react/display-name
const CreateUsersForm = forwardRef((_, forwardRef) => {
  const classes = useStyles();
  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.users.create'
  });
  const queryClient = useQueryClient();

  const [, { setCurrentPage }] = (useContext(UserViewContext) as unknown) as UserContextMethods;

  const methods = useForm<RbacUserWithResources>({
    defaultValues: initialFormValues,
    resolver: yupResolver(getUserValidationSchema(t))
  });
  const doCreateUser = useMutation(
    () => {
      return createUser(methods.getValues());
    },
    {
      onSuccess: () => {
        toast.success(t('form.successMsg', { user_email: methods.getValues().email }));
        queryClient.invalidateQueries('users');
        setCurrentPage(UserPages.LIST_USER);
      },
      onError: (err) => {
        toast.error(createErrorMessage(err));
      }
    }
  );

  const {
    formState: { errors }
  } = methods;

  const onSave = () => {
    methods.handleSubmit(() => {
      doCreateUser.mutate();
    })();
  };

  useImperativeHandle(
    forwardRef,
    () => ({
      onSave
    }),
    [onSave]
  );

  return (
    <FormProvider {...methods}>
      <Box className={classes.root}>
        <div className={classes.title}>{t('title')}</div>
        <form className={classes.form}>
          <YBInputField
            name="email"
            control={methods.control}
            label={t('form.email')}
            placeholder={t('form.emailPlaceholder')}
            fullWidth
          />
          <YBPasswordField
            name="password"
            control={methods.control}
            label={t('form.password')}
            placeholder={t('form.password')}
            fullWidth
          />
          <YBPasswordField
            name="confirmPassword"
            control={methods.control}
            label={t('form.confirmPassword')}
            placeholder={t('form.confirmPassword')}
            fullWidth
          />
          <RolesAndResourceMapping />
          {errors.roleResourceDefinitions?.message && (
            <FormHelperText required error>
              {errors.roleResourceDefinitions.message}
            </FormHelperText>
          )}
        </form>
      </Box>
    </FormProvider>
  );
});

export const CreateUsers = () => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.users.create'
  });
  const [, { setCurrentPage, setCurrentUser }] = (useContext(
    UserViewContext
  ) as unknown) as UserContextMethods;

  const createUserRef = useRef<any>(null);

  return (
    <Container
      onCancel={() => {
        setCurrentUser(null);
        setCurrentPage(UserPages.LIST_USER);
      }}
      onSave={() => {
        createUserRef.current?.onSave();
      }}
      saveLabel={t('title')}
    >
      <CreateUsersForm ref={createUserRef} />
    </Container>
  );
};

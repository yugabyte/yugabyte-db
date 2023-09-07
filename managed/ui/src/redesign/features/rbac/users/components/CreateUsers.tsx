/*
 * Created on Mon Jul 31 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useContext } from 'react';
import { useTranslation } from 'react-i18next';
import { FormProvider, useForm } from 'react-hook-form';
import { Box, makeStyles } from '@material-ui/core';
import Container from '../../common/Container';
import { RolesAndResourceMapping } from '../../policy/RolesAndResourceMapping';
import { YBInputField, YBPasswordField } from '../../../../components';
import { RbacUserWithResources } from '../interface/Users';
import { Resource } from '../../permission';
import { UserContextMethods, UserViewContext } from './UserContext';

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
      roleUUID: '',
      resourceGroup: {
        resourceDefinitionSet: [
          {
            allowAll: false,
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
  roleResourceDefinitions: initialMappingValue.roleResourceDefinitions
  // mappings: [initialMappingValue]
};

// eslint-disable-next-line react/display-name
const CreateUsersForm = forwardRef(() => {
  const classes = useStyles();
  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.users.create'
  });
  const methods = useForm<RbacUserWithResources>({ defaultValues: initialFormValues });

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
  return (
    <Container
      onCancel={() => {
        setCurrentUser(null);
        setCurrentPage('LIST_USER');
      }}
      onSave={() => {}}
      saveLabel={t('title')}
    >
      <CreateUsersForm />
    </Container>
  );
};

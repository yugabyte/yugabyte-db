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
import { useMutation, useQuery } from 'react-query';
import { toast } from 'react-toastify';

import { Box, makeStyles } from '@material-ui/core';
import Container from '../../common/Container';
import { YBLoadingCircleIcon } from '../../../../../components/common/indicators';
import { RolesAndResourceMapping } from '../../policy/RolesAndResourceMapping';
import { YBButton, YBInputField } from '../../../../components';
import { editUsersRoles, getRoleBindingsForUser } from '../../api';
import { convertRbacBindingsToUISchema } from './UserUtils';

import { RbacUserWithResources } from '../interface/Users';
import { UserContextMethods, UserViewContext } from './UserContext';

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
    defaultValues: currentUser ?? {}
  });

  const editUser = useMutation(() => editUsersRoles(currentUser!.uuid!, methods.getValues()), {
    onSuccess() {
      toast.success('Done');
      setCurrentPage('LIST_USER');
    }
  });

  const { isLoading } = useQuery(
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

  return (
    <Container
      onCancel={() => {
        setCurrentUser(null);
        setCurrentPage('LIST_USER');
      }}
      onSave={() => {
        editUser.mutate();
      }}
      saveLabel={t('title')}
    >
      <Box className={classes.root}>
        <div className={classes.header}>
          <div className={classes.back}>
            <ArrowLeft
              onClick={() => {
                setCurrentPage('LIST_USER');
                setCurrentUser(null);
              }}
            />
            {t('title')}
          </div>
          <YBButton variant="secondary" size="large" startIcon={<Delete />} onClick={() => {}}>
            {t('delete')}
          </YBButton>
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
          </form>
        </FormProvider>
      </Box>
    </Container>
  );
};

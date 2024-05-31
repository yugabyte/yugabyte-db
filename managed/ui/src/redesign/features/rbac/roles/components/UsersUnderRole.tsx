/*
 * Created on Thu Oct 05 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useContext, useImperativeHandle } from 'react';
import { useQuery } from 'react-query';
import Cookies from 'js-cookie';
import { flattenDeep, values } from 'lodash';
import { useTranslation } from 'react-i18next';
import { Typography, makeStyles } from '@material-ui/core';
import { getRoleBindingsForAllUsers } from '../../api';
import { Pages, RoleContextMethods, RoleViewContext } from '../RoleContext';
import { UsersTab } from '../../common/rbac_constants';

const useStyles = makeStyles((theme) => ({
  root: {
    minHeight: '300px',
    padding: theme.spacing(3),
    paddingTop: '6px'
  },
  header: {
    marginLeft: theme.spacing(1)
  },
  userArea: {
    borderRadius: theme.spacing(1),
    border: `1px solid ${theme.palette.grey[200]}`,
    padding: theme.spacing(2),
    minHeight: '300px',
    width: '550px',
    marginTop: theme.spacing(2)
  },
  userItem: {
    marginBottom: theme.spacing(2),
    textDecoration: 'underline',
    color: theme.palette.grey[900],
    cursor: 'pointer'
  }
}));

// eslint-disable-next-line react/display-name
export const UsersUnderRole = forwardRef((_, forwardRef) => {
  const [{ currentRole }, { setCurrentPage, setCurrentRole }] = (useContext(
    RoleViewContext
  ) as unknown) as RoleContextMethods;

  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.roles.edit.viewUsers'
  });

  const classes = useStyles();

  const userId = Cookies.get('userId') ?? localStorage.getItem('userId');

  const { data: roleBindings } = useQuery(['role_bindings', userId], getRoleBindingsForAllUsers, {
    select: (data) => data.data
  });

  const usersWithThisRole = flattenDeep(values(roleBindings ?? [])).filter(
    (bindings) => bindings.role.roleUUID === currentRole?.roleUUID
  );

  const onCancel = () => {
    setCurrentRole(null);
    setCurrentPage(Pages.LIST_ROLE);
  };

  useImperativeHandle(
    forwardRef,
    () => ({
      onCancel
    }),
    [onCancel]
  );

  if (!roleBindings) return <div className={classes.root}></div>;

  return (
    <div className={classes.root}>
      <Typography variant="body1" className={classes.header}>
        {usersWithThisRole.length === 0 ? t('noUsersAssgined') : t('assignedUser')}
      </Typography>
      {usersWithThisRole.length !== 0 && (
        <div className={classes.userArea}>
          {usersWithThisRole.map((user) => (
            <Typography
              variant="body2"
              className={classes.userItem}
              onClick={() => {
                window.open(`${UsersTab}&userUUID=${user.user.uuid}`, '_blank');
              }}
            >
              {user.user.email}
            </Typography>
          ))}
        </div>
      )}
    </div>
  );
});

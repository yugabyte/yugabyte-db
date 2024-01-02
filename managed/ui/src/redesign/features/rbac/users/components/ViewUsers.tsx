/*
 * Created on Mon Jul 31 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useContext, useState } from 'react';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';

import { Box, makeStyles } from '@material-ui/core';
import { TableHeaderColumn } from 'react-bootstrap-table';
import { YBTable } from '../../../../../components/common/YBTable';
import { MoreActionsMenu } from '../../../../../components/customCACerts/MoreActionsMenu';
import { YBButton } from '../../../../components';
import { YBLoadingCircleIcon } from '../../../../../components/common/indicators';
import { YBSearchInput } from '../../../../../components/common/forms/fields/YBSearchInput';

import { getAllUsers } from '../../api';
import { ybFormatDate } from '../../../../helpers/DateUtils';
import { UserContextMethods, UserViewContext } from './UserContext';
import { RbacUser } from '../interface/Users';
import { Add, ArrowDropDown } from '@material-ui/icons';
import { ReactComponent as User } from '../../../../assets/user.svg';
import { ReactComponent as Delete } from '../../../../assets/trashbin.svg';

const useStyles = makeStyles((theme) => ({
  root: {
    padding: `${theme.spacing(5.5)}px ${theme.spacing(3)}px`,
    '& .yb-table-header th,.yb-table-row td': {
      paddingLeft: '0 !important'
    }
  },
  moreActionsBut: {
    height: '30px',
    padding: '5px 10px',
    borderRadius: theme.spacing(0.75),
    border: '1px solid #C8C8C8',
    background: theme.palette.common.white,
    justifyContent: 'center',
    display: 'flex',
    alignItems: 'center',
    userSelect: 'none',
    width: '90px',
    '& svg': {
      width: theme.spacing(3),
      height: theme.spacing(3)
    }
  },
  roleType: {
    borderRadius: theme.spacing(0.5),
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    padding: '2px 6px',
    '&.custom': {
      border: `1px solid ${theme.palette.primary[300]}`,
      background: theme.palette.primary[200],
      color: theme.palette.primary[600]
    }
  },
  actions: {
    display: 'flex',
    justifyContent: 'space-between',
    marginBottom: theme.spacing(3)
  },
  title: {
    fontSize: '18px',
    fontWeight: 600,
    marginRight: theme.spacing(4)
  },
  search: {
    display: 'flex',
    alignItems: 'center',
    '& .search-input': {
      width: '380px'
    }
  }
}));

export const ViewUsers = () => {
  const classes = useStyles();

  const { isLoading, data: roles } = useQuery('users', getAllUsers, {
    select: (data) => data.data
  });

  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.users.list'
  });

  const [, { setCurrentPage, setCurrentUser }] = (useContext(
    UserViewContext
  ) as unknown) as UserContextMethods;

  const [searchText, setSearchText] = useState('');

  if (isLoading) return <YBLoadingCircleIcon />;

  let filteredRoles = roles;

  if (searchText) {
    filteredRoles = filteredRoles?.filter((user: RbacUser) =>
      user.email.toLowerCase().includes(searchText.toLowerCase())
    );
  }

  const getActions = (_: undefined, user: RbacUser) => {
    return (
      <MoreActionsMenu
        menuOptions={[
          {
            text: t('table.moreActions.editAssignedRoles'),
            icon: <User />,
            callback: () => {
              setCurrentUser(user);
              setCurrentPage('EDIT_USER');
            }
          },
          {
            text: t('table.moreActions.deleteUser'),
            icon: <Delete />,
            callback: () => {}
          }
        ]}
      >
        <span className={classes.moreActionsBut}>
          {t('table.actions')} <ArrowDropDown />
        </span>
      </MoreActionsMenu>
    );
  };

  return (
    <Box className={classes.root}>
      <div className={classes.actions}>
        <div className={classes.search}>
          <div className={classes.title}>{t('rowsCount', { count: roles?.length })}</div>
          <YBSearchInput
            placeHolder={t('search')}
            onEnterPressed={(val: string) => setSearchText(val)}
          />
        </div>
        <YBButton
          startIcon={<Add />}
          size="large"
          variant="primary"
          onClick={() => {
            setCurrentUser(null);
            setCurrentPage('CREATE_USER');
          }}
        >
          {t('createUser')}
        </YBButton>
      </div>
      <YBTable data={filteredRoles ?? []}>
        <TableHeaderColumn dataField="uuid" hidden isKey />
        <TableHeaderColumn dataSort dataField="email">
          {t('table.email')}
        </TableHeaderColumn>
        <TableHeaderColumn dataSort dataField="createdAt" dataFormat={(cell) => ybFormatDate(cell)}>
          {t('table.createdAt')}
        </TableHeaderColumn>
        <TableHeaderColumn dataField="actions" dataFormat={getActions}>
          {t('table.actions')}
        </TableHeaderColumn>
      </YBTable>
    </Box>
  );
};

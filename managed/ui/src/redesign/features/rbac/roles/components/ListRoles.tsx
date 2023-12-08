/*
 * Created on Wed Jul 12 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useContext, useState } from 'react';
import clsx from 'clsx';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { useToggle } from 'react-use';
import { Box, makeStyles } from '@material-ui/core';
import { TableHeaderColumn } from 'react-bootstrap-table';
import { YBTable } from '../../../../../components/common/YBTable';
import { MoreActionsMenu } from '../../../../../components/customCACerts/MoreActionsMenu';
import { DeleteRoleModal } from './DeleteRoleModal';
import { YBButton } from '../../../../components';
import { YBLoadingCircleIcon } from '../../../../../components/common/indicators';
import { YBSearchInput } from '../../../../../components/common/forms/fields/YBSearchInput';

import { RoleContextMethods, RoleViewContext } from '../RoleContext';
import { IRole } from '../IRoles';
import { getAllRoles } from '../../api';

import { Add, ArrowDropDown } from '@material-ui/icons';
import { ReactComponent as Create } from '../../../../assets/edit_pen.svg';
import { ReactComponent as Clone } from '../../../../assets/copy.svg';
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
    marginRight: '38px'
  },
  search: {
    display: 'flex',
    alignItems: 'center',
    '& .search-input': {
      width: '380px'
    }
  }
}));

const ListRoles = () => {
  const classes = useStyles();

  const { isLoading, data: roles } = useQuery('roles', getAllRoles, {
    select: (data) => data.data
  });

  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.roles.list'
  });

  const [, { setCurrentPage, setCurrentRole }] = (useContext(
    RoleViewContext
  ) as unknown) as RoleContextMethods;
  const [showDeleteModal, toggleDeleteModal] = useToggle(false);
  const [searchText, setSearchText] = useState('');

  if (isLoading) return <YBLoadingCircleIcon />;

  let filteredRoles = roles;

  if (searchText) {
    filteredRoles = filteredRoles?.filter((role) =>
      role.name.toLowerCase().includes(searchText.toLowerCase())
    );
  }

  const getActions = (_: undefined, role: IRole) => {
    return (
      <MoreActionsMenu
        menuOptions={[
          {
            text: t('table.moreActions.editRole'),
            icon: <Create />,
            callback: () => {
              setCurrentRole(role);
              setCurrentPage('EDIT_ROLE');
            }
          },
          {
            text: t('table.moreActions.cloneRole'),
            icon: <Clone />,
            callback: () => {}
          },
          {
            text: t('table.moreActions.viewUsers'),
            icon: <User />,
            callback: () => {}
          },
          {
            text: t('table.moreActions.deleteRole'),
            icon: <Delete />,
            callback: () => {
              setCurrentRole(role);
              toggleDeleteModal(true);
            }
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
            setCurrentRole(null);
            setCurrentPage('CREATE_ROLE');
          }}
        >
          {t('createRole')}
        </YBButton>
      </div>
      <YBTable data={filteredRoles ?? []}>
        <TableHeaderColumn dataField="roleUUID" hidden isKey />
        <TableHeaderColumn dataSort dataField="name">
          {t('table.name')}
        </TableHeaderColumn>
        <TableHeaderColumn dataSort dataField="description" dataFormat={(desc) => desc ?? '-'}>
          {t('table.description')}
        </TableHeaderColumn>
        <TableHeaderColumn
          dataSort
          dataField="roleType"
          dataFormat={(t: IRole['roleType']) => (
            <span className={clsx(classes.roleType, t === 'Custom' && 'custom')}>{t}</span>
          )}
        >
          {t('table.type')}
        </TableHeaderColumn>
        <TableHeaderColumn dataSort dataField="users">
          {t('table.users')}
        </TableHeaderColumn>
        <TableHeaderColumn dataField="actions" dataFormat={getActions}>
          {t('table.actions')}
        </TableHeaderColumn>
      </YBTable>
      <DeleteRoleModal open={showDeleteModal} onHide={() => toggleDeleteModal(false)} />
    </Box>
  );
};

export default ListRoles;

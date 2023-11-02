/*
 * Created on Wed Jul 12 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useContext, useState } from 'react';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { find, flattenDeep, values } from 'lodash';
import { useToggle } from 'react-use';
import { Box, makeStyles } from '@material-ui/core';
import { TableHeaderColumn } from 'react-bootstrap-table';
import { YBTable } from '../../../../../components/common/YBTable';
import { MoreActionsMenu } from '../../../../../components/customCACerts/MoreActionsMenu';
import { DeleteRoleModal } from './DeleteRoleModal';
import { YBButton } from '../../../../components';
import { YBLoadingCircleIcon } from '../../../../../components/common/indicators';
import { YBSearchInput } from '../../../../../components/common/forms/fields/YBSearchInput';
import { RoleTypeComp } from '../../common/RbacUtils';

import { EditViews, Pages, RoleContextMethods, RoleViewContext } from '../RoleContext';
import { ForbiddenRoles, Role, RoleType } from '../IRoles';
import { getAllRoles, getRoleBindingsForAllUsers } from '../../api';
import { RbacValidator, hasNecessaryPerm } from '../../common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../ApiAndUserPermMapping';
import { SortOrder } from '../../../../helpers/constants';

import { Add, ArrowDropDown } from '@material-ui/icons';
import { ReactComponent as Create } from '../../../../assets/edit_pen.svg';
import { ReactComponent as Clone } from '../../../../assets/copy.svg';
import { ReactComponent as Delete } from '../../../../assets/trashbin.svg';
import { ReactComponent as User } from '../../../../assets/user.svg';

const useStyles = makeStyles((theme) => ({
  root: {
    padding: `${theme.spacing(5.5)}px ${theme.spacing(3)}px`,
    '& .yb-table-header th,.yb-table-row td': {
      paddingLeft: '0 !important'
    },
    '& .yb-table-row': {
      cursor: 'default'
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

  const { data: roleBindings } = useQuery('role_bindings', getRoleBindingsForAllUsers, {
    select: (data) => data.data
  });

  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.roles.list'
  });

  const [, { setCurrentPage, setCurrentRole, setEditView }] = (useContext(
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

  const getActions = (_: undefined, role: Role) => {
    const menuOptions = [
      {
        text: t('table.moreActions.viewUsers'),
        icon: <User />,
        callback: () => {
          setCurrentRole(role);
          setEditView(EditViews.USERS);
          setCurrentPage(Pages.EDIT_ROLE);
        },
        menuItemWrapper(elem: JSX.Element) {
          return elem;
        },
        disabled: false
      }
    ];

    menuOptions.push({
      text: t('table.moreActions.editRole'),
      icon: <Create />,
      callback: () => {
        setCurrentRole(role);
        setEditView(EditViews.CONFIGURATIONS);
        setCurrentPage(Pages.EDIT_ROLE);
      },
      menuItemWrapper(elem) {
        return (
          <RbacValidator
            isControl
            accessRequiredOn={{
              ...ApiPermissionMap.MODIFY_RBAC_ROLE,
              onResource: { ROLE: role.roleUUID }
            }}
            overrideStyle={{ display: 'block' }}
          >
            {elem}
          </RbacValidator>
        );
      },
      disabled: role.roleType === RoleType.SYSTEM
    });

    menuOptions.push({
      text: t('table.moreActions.cloneRole'),
      icon: <Clone />,
      callback: () => {
        setCurrentRole({
          ...role,
          roleUUID: '',
          name: ''
        });
        setEditView(EditViews.CONFIGURATIONS);
        setCurrentPage(Pages.CREATE_ROLE);
      },
      menuItemWrapper(elem) {
        return (
          <RbacValidator
            isControl
            accessRequiredOn={{ ...ApiPermissionMap.CREATE_RBAC_ROLE }}
            customValidateFunction={() => {
              return hasNecessaryPerm({
                ...ApiPermissionMap.CREATE_RBAC_ROLE,
                onResource: { ROLE: role.roleUUID }
              });
            }}
            overrideStyle={{ display: 'block' }}
          >
            {elem}
          </RbacValidator>
        );
      },
      disabled: find(ForbiddenRoles, { name: role.name, roleType: role.roleType }) !== undefined
    });

    menuOptions.push({
      text: t('table.moreActions.deleteRole'),
      icon: <Delete />,
      callback: () => {
        setCurrentRole(role);
        toggleDeleteModal(true);
      },
      menuItemWrapper(elem) {
        return (
          <RbacValidator
            isControl
            overrideStyle={{ display: 'block' }}
            accessRequiredOn={{
              ...ApiPermissionMap.DELETE_RBAC_ROLE,
              onResource: { ROLE: role.roleUUID }
            }}
            customValidateFunction={() =>
              hasNecessaryPerm({
                ...ApiPermissionMap.DELETE_RBAC_ROLE,
                onResource: { ROLE: role.roleUUID }
              })
            }
          >
            {elem}
          </RbacValidator>
        );
      },
      disabled: role.roleType === RoleType.SYSTEM
    });

    return (
      <MoreActionsMenu menuOptions={menuOptions}>
        <span className={classes.moreActionsBut}>
          {t('table.actions')} <ArrowDropDown />
        </span>
      </MoreActionsMenu>
    );
  };

  const allRoleMapping = flattenDeep(values(roleBindings ?? []));

  return (
    <RbacValidator accessRequiredOn={ApiPermissionMap.GET_RBAC_ROLES}>
      <Box className={classes.root}>
        <div className={classes.actions}>
          <div className={classes.search}>
            <div className={classes.title} data-testid="roles-count">
              {t('rowsCount', { count: roles?.length })}
            </div>
            <YBSearchInput
              placeHolder={t('search')}
              onEnterPressed={(val: string) => setSearchText(val)}
            />
          </div>
          <RbacValidator
            accessRequiredOn={{
              ...ApiPermissionMap.CREATE_RBAC_ROLE
            }}
            isControl
          >
            <YBButton
              startIcon={<Add />}
              size="large"
              variant="primary"
              onClick={() => {
                setCurrentRole(null);
                setCurrentPage(Pages.CREATE_ROLE);
              }}
              data-testid={`rbac-resource-create-role`}
            >
              {t('createRole')}
            </YBButton>
          </RbacValidator>
        </div>
        <YBTable data={filteredRoles ?? []}>
          <TableHeaderColumn dataField="roleUUID" hidden isKey />
          <TableHeaderColumn dataSort dataField="name">
            {t('table.name')}
          </TableHeaderColumn>
          <TableHeaderColumn
            dataSort
            dataField="description"
            width="35%"
            dataFormat={(desc) => (desc ? desc : '-')}
          >
            {t('table.description')}
          </TableHeaderColumn>
          <TableHeaderColumn
            dataSort
            dataField="roleType"
            width="10%"
            dataFormat={(_, role: Role) => <RoleTypeComp role={role} />}
          >
            {t('table.type')}
          </TableHeaderColumn>
          <TableHeaderColumn
            width="10%"
            dataSort
            dataField="users"
            sortFunc={(a: Role, b: Role, order) => {
              const aCount = allRoleMapping.filter(
                (roleMapping) => roleMapping.role.roleUUID === a.roleUUID
              ).length;
              const bCount = allRoleMapping.filter(
                (roleMapping) => roleMapping.role.roleUUID === b.roleUUID
              ).length;
              return order === SortOrder.ASCENDING ? aCount - bCount : bCount - aCount;
            }}
            dataFormat={(_, role: Role) => (
              <>
                {
                  allRoleMapping.filter(
                    (roleMapping) => roleMapping.role.roleUUID === role.roleUUID
                  ).length
                }
              </>
            )}
          >
            {t('table.users')}
          </TableHeaderColumn>
          <TableHeaderColumn dataField="actions" dataFormat={getActions}>
            {t('table.actions')}
          </TableHeaderColumn>
        </YBTable>
        <DeleteRoleModal open={showDeleteModal} onHide={() => toggleDeleteModal(false)} />
      </Box>
    </RbacValidator>
  );
};

export default ListRoles;

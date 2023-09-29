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
import { flattenDeep, values } from 'lodash';
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

import { RoleContextMethods, RoleViewContext } from '../RoleContext';
import { Role } from '../IRoles';
import { getAllRoles, getRoleBindingsForAllUsers } from '../../api';
import { RBAC_ERR_MSG_NO_PERM, RbacValidator, hasNecessaryPerm } from '../../common/RbacValidator';
import { UserPermissionMap } from '../../UserPermPathMapping';

import { Add, ArrowDropDown } from '@material-ui/icons';
import { ReactComponent as Create } from '../../../../assets/edit_pen.svg';
import { ReactComponent as Clone } from '../../../../assets/copy.svg';
import { ReactComponent as Delete } from '../../../../assets/trashbin.svg';
import { toast } from 'react-toastify';

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

  const getActions = (_: undefined, role: Role) => {
    return (
      <MoreActionsMenu
        menuOptions={[
          {
            text: t('table.moreActions.editRole'),
            icon: <Create />,
            callback: () => {
              if (
                !hasNecessaryPerm({
                  ...UserPermissionMap.editRole,
                  onResource: role.roleUUID
                })
              ) {
                toast.error(RBAC_ERR_MSG_NO_PERM);
                return;
              }
              setCurrentRole(role);
              setCurrentPage('EDIT_ROLE');
            }
          },
          {
            text: t('table.moreActions.cloneRole'),
            icon: <Clone />,
            callback: () => {
              setCurrentRole({
                ...role,
                roleUUID: '',
                name: ''
              });
              setCurrentPage('CREATE_ROLE');
            }
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

  const allRoleMapping = flattenDeep(values(roleBindings ?? []));

  return (
    <RbacValidator
      accessRequiredOn={{ onResource: 'CUSTOMER_ID', ...UserPermissionMap.listRole }}
      customValidateFunction={() => true}
    >
      <Box className={classes.root}>
        <div className={classes.actions}>
          <div className={classes.search}>
            <div className={classes.title}>{t('rowsCount', { count: roles?.length })}</div>
            <YBSearchInput
              placeHolder={t('search')}
              onEnterPressed={(val: string) => setSearchText(val)}
            />
          </div>
          <RbacValidator
            accessRequiredOn={{
              onResource: undefined,
              ...UserPermissionMap.createRole
            }}
            isControl
          >
            <YBButton
              startIcon={<Add />}
              size="large"
              variant="primary"
              onClick={() => {
                setCurrentRole(null);
                setCurrentPage('CREATE_ROLE');
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
            width="45%"
            dataFormat={(desc) => desc ?? '-'}
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

/* eslint-disable no-console */
/*
 * Created on Mon Jul 08 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useState } from 'react';
import { useToggle } from 'react-use';
import { useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';
import { isEmpty } from 'lodash';

import { Box, makeStyles } from '@material-ui/core';
import { TableHeaderColumn } from 'react-bootstrap-table';
import { MoreActionsMenu } from '../../../../../components/customCACerts/MoreActionsMenu';
import { YBSearchInput } from '../../../../../components/common/forms/fields/YBSearchInput';
import { YBButton } from '../../../../components';
import { YBTable } from '../../../../../components/common/YBTable';
import { YBErrorIndicator, YBLoadingCircleIcon } from '../../../../../components/common/indicators';
import { DeleteGroupModal } from './DeleteGroup';
import { GroupEmpty } from './GroupEmpty';

import { useListMappings } from '../../../../../v2/api/authentication/authentication';
import { api } from '../../../universe/universe-form/utils/api';
import { getAllRoles } from '../../api';
import { ybFormatDate } from '../../../../helpers/DateUtils';
import { RbacValidator, hasNecessaryPerm } from '../../common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../ApiAndUserPermMapping';
import { RoleTypeComp } from '../../common/RbacUtils';
import { Role, RoleType } from '../../roles';
import {
  AuthGroupToRolesMapping,
  AuthGroupToRolesMappingType
} from '../../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';
import {
  GetGroupContext,
  getIsLDAPEnabled,
  getIsOIDCEnabled,
  OIDC_RUNTIME_CONFIGS_QUERY_KEY,
  WrapDisabledElements
} from './GroupUtils';

import { Pages } from './GroupContext';
import { YBTag, YBTag_Types } from '../../../../../components/common/YBTag';
import { Add, ArrowDropDown } from '@material-ui/icons';
import { ReactComponent as Create } from '../../../../assets/edit_pen.svg';
import { ReactComponent as Delete } from '../../../../assets/trashbin.svg';

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
    justifyContent: 'end',
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
  },
  rolesList: {
    overflow: 'unset !important',
    '& > span': {
      marginRight: '6px'
    }
  },
  rolesTd: {
    overflow: 'unset !important',
    display: 'flex'
  },
  moreRoles: {
    color: '#67666C'
  },
  inactive: {
    color: theme.palette.grey[600]
  },
  groupName: {
    padding: '5px'
  }
}));

const ListGroups = () => {
  const classes = useStyles();

  const { data: groups, isLoading, isError, error } = useListMappings();

  const { isLoading: isRolesLoading, data: roles, isError: isRolesFailed } = useQuery(
    'roles',
    getAllRoles,
    {
      select: (data) => data.data
    }
  );

  const {
    data: runtimeConfig,
    isLoading: isRuntimeConfigLoading,
    isError: isRuntimeConfigFailed
  } = useQuery(OIDC_RUNTIME_CONFIGS_QUERY_KEY, () => api.fetchRunTimeConfigs(true));

  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.groups.list'
  });
  const [, { setCurrentPage, setCurrentGroup }] = GetGroupContext();

  const [showDeleteModal, toggleDeleteModal] = useToggle(false);
  const [searchText, setSearchText] = useState('');

  if (isLoading || isRolesLoading || isRuntimeConfigLoading || !runtimeConfig)
    return <YBLoadingCircleIcon />;

  if (isError || isRolesFailed || isRuntimeConfigFailed)
    return (
      <Box className={classes.root}>
        <YBErrorIndicator customErrorMessage={(error?.response?.data as any).error} />
      </Box>
    );

  const isOIDCEnabled = getIsOIDCEnabled(runtimeConfig);
  const isLDAPEnabled = getIsLDAPEnabled(runtimeConfig);

  if (isEmpty(groups)) {
    return <GroupEmpty noAuthProviderConfigured={!isOIDCEnabled && !isLDAPEnabled} />;
  }

  let filteredGroups = groups;

  if (searchText) {
    filteredGroups = filteredGroups?.filter((group) =>
      group.group_identifier.toLowerCase().includes(searchText.toLowerCase())
    );
  }

  const getActions = (_: undefined, group: AuthGroupToRolesMapping) => {
    const menuOptions = [];

    menuOptions.push({
      text: t('table.moreActions.editGroup'),
      icon: <Create />,
      callback: () => {
        setCurrentGroup(group);
        setCurrentPage(Pages.CREATE_GROUP);
      },
      menuItemWrapper(elem: JSX.Element) {
        return (
          <RbacValidator
            isControl
            accessRequiredOn={{
              ...ApiPermissionMap.MODIFY_RBAC_ROLE,
              onResource: { ROLE: group.uuid }
            }}
            overrideStyle={{ display: 'block' }}
          >
            {elem}
          </RbacValidator>
        );
      },
      disabled: false
    });

    menuOptions.push({
      text: t('table.moreActions.deleteGroup'),
      icon: <Delete />,
      callback: () => {
        setCurrentGroup(group);
        toggleDeleteModal(true);
      },
      menuItemWrapper(elem: JSX.Element) {
        return (
          <RbacValidator
            isControl
            overrideStyle={{ display: 'block' }}
            accessRequiredOn={{
              ...ApiPermissionMap.DELETE_RBAC_ROLE,
              onResource: { ROLE: group.uuid }
            }}
            customValidateFunction={() =>
              hasNecessaryPerm({
                ...ApiPermissionMap.DELETE_RBAC_ROLE,
                onResource: { ROLE: group.uuid }
              })
            }
          >
            {elem}
          </RbacValidator>
        );
      },
      disabled: false
    });

    return (
      <MoreActionsMenu menuOptions={menuOptions}>
        <span className={classes.moreActionsBut}>
          {t('table.actions')} <ArrowDropDown />
        </span>
      </MoreActionsMenu>
    );
  };

  return (
    <RbacValidator accessRequiredOn={ApiPermissionMap.GET_RBAC_ROLES}>
      <Box className={classes.root}>
        <div className={classes.actions}>
          <div className={classes.search}>
            <div className={classes.title} data-testid="roles-count">
              {t('rowsCount', { count: groups?.length })}
            </div>
            <YBSearchInput
              placeHolder={t('search')}
              onEnterPressed={(val: string) => setSearchText(val)}
              data-testid="group-search"
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
                setCurrentGroup(null);
                setCurrentPage(Pages.CREATE_GROUP);
              }}
              data-testid={`rbac-resource-create-role`}
            >
              {t('createGroup')}
            </YBButton>
          </RbacValidator>
        </div>
        <YBTable data={filteredGroups ?? []}>
          <TableHeaderColumn dataField="uuid" hidden isKey />
          <TableHeaderColumn
            dataSort
            width="20%"
            dataField="group_identifier"
            dataFormat={(name, row: AuthGroupToRolesMapping) => {
              if (
                (row.type === AuthGroupToRolesMappingType.LDAP && isLDAPEnabled) ||
                (row.type === AuthGroupToRolesMappingType.OIDC && isOIDCEnabled)
              ) {
                return <span className={classes.groupName}>{name}</span>;
              }
              return WrapDisabledElements(
                <span className={`${classes.inactive} ${classes.groupName}`}>
                  {name}
                  <YBTag type={YBTag_Types.YB_GRAY}>{t('inactive')}</YBTag>
                </span>,
                true,
                t('groupDisabled', { auth_provider: row.type })
              );
            }}
          >
            {t('table.name')}
          </TableHeaderColumn>
          <TableHeaderColumn
            dataSort
            dataField="type"
            width="8%"
            dataFormat={(type) => (type ? <span title={type}>{type}</span> : '-')}
          >
            {t('table.authProvider')}
          </TableHeaderColumn>
          <TableHeaderColumn
            dataSort
            dataField="role_resource_definitions"
            width="25%"
            dataFormat={(_role, row: AuthGroupToRolesMapping) => {
              const rolesAssociated = row.role_resource_definitions.map((r) => r.role_uuid);
              const rolesToDisplay = roles?.filter((r) => rolesAssociated.includes(r.roleUUID));

              if (rolesToDisplay && rolesToDisplay.length > 0) {
                const minRoles = rolesToDisplay.splice(4);
                return (
                  <div className={classes.rolesTd}>
                    <div className={classes.rolesList}>
                      {rolesToDisplay.map((role: Role) => (
                        <RoleTypeComp role={role} customLabel={role.name} />
                      ))}
                    </div>
                    {minRoles.length > 0 && (
                      <span className={classes.moreRoles}>
                        {t('table.moreRoles', {
                          count: minRoles.length,
                          keyPrefix: 'rbac.users.list'
                        })}
                      </span>
                    )}
                  </div>
                );
              } else {
                return (
                  <RoleTypeComp
                    role={{ roleType: RoleType.SYSTEM } as Role}
                    customLabel={t('table.connectOnly', { keyPrefix: 'rbac.users.list' })}
                  />
                );
              }
            }}
          >
            {t('table.roles')}
          </TableHeaderColumn>
          <TableHeaderColumn
            dataSort
            dataField="creationDate"
            dataFormat={(cell) => ybFormatDate(cell)}
            width="15%"
          >
            {t('table.createdAt')}
          </TableHeaderColumn>
          <TableHeaderColumn width="7%" dataField="actions" dataFormat={getActions}>
            {t('table.actions')}
          </TableHeaderColumn>
        </YBTable>
        <DeleteGroupModal
          open={showDeleteModal}
          onHide={() => {
            setCurrentGroup(null);
            toggleDeleteModal(false);
          }}
        />
      </Box>
    </RbacValidator>
  );
};

export default ListGroups;

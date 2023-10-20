/*
 * Created on Wed Aug 02 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useState } from 'react';
import { find } from 'lodash';
import clsx from 'clsx';
import { useFieldArray, useFormContext } from 'react-hook-form';
import { useQuery } from 'react-query';
import { Trans, useTranslation } from 'react-i18next';
import { Box, MenuItem, Typography, makeStyles } from '@material-ui/core';
import { YBSelect } from '../../../components';
import { YBLoadingCircleIcon } from '../../../../components/common/indicators';

import { getAllRoles } from '../api';
import { fetchUniversesList } from '../../../../actions/xClusterReplication';
import { Universe } from '../../../helpers/dtos';
import { SelectUniverseResource } from './SelectUniverseResource';
import { RbacUserWithResources } from '../users/interface/Users';
import { initialMappingValue } from '../users/components/CreateUsers';
import { ForbiddenRoles, Role } from '../roles';

import { ReactComponent as BulbIcon } from '../../../assets/bulb.svg';
import { ReactComponent as Close } from '../../../assets/close copy.svg';
import { YBTabPanel, YBTab, YBTabs } from '../../../components/YBTabs/YBTabs';

const useStyles = makeStyles((theme) => ({
  root: {
    marginTop: theme.spacing(4)
  },
  mappingsContainer: {
    borderRadius: theme.spacing(1),
    border: `1px solid ${theme.palette.ybacolors.backgroundGrayDark}`,
    background: theme.palette.ybacolors.backgroundGrayLightest,
    padding: theme.spacing(2),
    paddingBottom: theme.spacing(3),
    width: '1080px',
    marginTop: theme.spacing(2)
  },
  mappingRow: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(2)
  },
  roleMargin: {
    marginBottom: theme.spacing(2)
  },
  roleSelect: {
    minWidth: '500px',
    '& .MuiInputBase-formControl': {
      padding: theme.spacing(1)
    }
  },
  assignNewRole: {
    color: '#EF5824',
    display: 'flex',
    cursor: 'pointer',
    userSelect: 'none',
    alignItems: 'center',
    marginLeft: '2px',
    '& > i': {
      fontSize: '24px',
      marginRight: theme.spacing(1)
    }
  },
  removeRoleMapping: {
    height: theme.spacing(3),
    '& > svg': {
      width: theme.spacing(3),
      height: theme.spacing(3),
      cursor: 'pointer'
    }
  },
  connectOnly: {
    color: '#67666C',
    width: '1500px',
    margin: `${theme.spacing(3)}px 0`
  },
  noUniverses: {
    borderRadius: theme.spacing(1),
    border: `1px solid ${theme.palette.ybacolors.backgroundGrayDark}`,
    background: theme.palette.ybacolors.backgroundGrayLight,
    padding: theme.spacing(1.25),
    width: '1080px',
    display: 'flex',
    gap: theme.spacing(1.25),
    marginTop: theme.spacing(2)
  },
  rolesCount: {
    borderRadius: '6px',
    background: 'rgba(229, 229, 233, 0.50)',
    padding: '3px 6px',
    marginLeft: '4px'
  },
  tabLabel: {
    display: 'flex',
    flexDirection: 'row',
    alignItems: 'center'
  },
  marginTop: {
    marginTop: theme.spacing(5.25)
  },
  title: {
    marginBottom: theme.spacing(0.8)
  }
}));

type RolesAndResourceMappingProps = {};

export const RolesAndResourceMapping: FC<RolesAndResourceMappingProps> = () => {
  const { isLoading: isRoleListLoading, data: roles } = useQuery('roles', getAllRoles, {
    select: (data) => data.data
  });
  const { isLoading: isUniverseListLoading, data: universeList } = useQuery<Universe[]>(
    ['universeList'],
    () => fetchUniversesList().then((res) => res.data),
    {
      refetchOnMount: false
    }
  );
  const classes = useStyles();

  const { control } = useFormContext<RbacUserWithResources>();
  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.permissions'
  });

  const { fields, append, remove, update } = useFieldArray({
    control,
    name: 'roleResourceDefinitions'
  });

  const [tab, setTabs] = useState<Role['roleType']>('System');
  const handleChange = (_event: any, newValue: any) => {
    setTabs(newValue);
  };

  if (isRoleListLoading || isUniverseListLoading) return <YBLoadingCircleIcon />;

  const ConnectOnly = (
    <div className={classes.connectOnly}>
      <Trans t={t} i18nKey="connectOnly" components={{ b: <b /> }} />
    </div>
  );

  const [BuiltinRoleTab, CustomRoleTab] = fields.reduce(
    (prevValue, field, index) => {
      const isForbidden =
        find(ForbiddenRoles, { name: field.role?.name, roleType: field.role?.roleType }) !==
        undefined;
      const component = (
        <Box key={field.id} className={classes.roleMargin}>
          <div className={classes.mappingRow}>
            <YBSelect
              name={`mappings.${index}.role`}
              className={classes.roleSelect}
              value={field.role?.roleUUID}
              onChange={(e) => {
                update(index, {
                  ...field,
                  role: find(roles, { roleUUID: e.target.value })!
                });
              }}
              data-testid={`rbac-role-select`}
              disabled={isForbidden}
            >
              {roles
                ?.filter((r) => r.roleType === tab)
                .map((role) => (
                  <MenuItem
                    key={role.roleUUID}
                    value={role.roleUUID}
                    data-testid={`rbac-role-select-${role.name}`}
                    disabled={
                      find(ForbiddenRoles, { name: role?.name, roleType: role?.roleType }) !==
                      undefined
                    }
                  >
                    {role.name}
                  </MenuItem>
                ))}
            </YBSelect>
            {field.roleType === 'Custom' && (
              <>
                {t('for')}
                <SelectUniverseResource universeList={universeList ?? []} fieldIndex={index} />
              </>
            )}
            {!isForbidden && (
              <span
                onClick={() => remove(index)}
                className={classes.removeRoleMapping}
                data-testid={`rbac-remove-role`}
              >
                <Close />
              </span>
            )}
          </div>
        </Box>
      );
      if (field.roleType === 'System') {
        prevValue[0].push(component);
      } else {
        prevValue[1].push(component);
      }
      return prevValue;
    },
    [[], []] as [JSX.Element[], JSX.Element[]]
  );

  return (
    <div className={classes.root}>
      <Typography variant="h4">{t('title')}</Typography>
      <YBTabs value={tab} onChange={handleChange}>
        <YBTab
          label={
            <span className={classes.tabLabel}>
              {t('builtInRoleTab')}
              <span className={classes.rolesCount}>{BuiltinRoleTab.length}</span>
            </span>
          }
          value={'System'}
          data-testid={`rbac-builtin-tab`}
        />
        <YBTab
          label={
            <span className={classes.tabLabel}>
              {t('customRoleTab')}
              <span className={classes.rolesCount}>{CustomRoleTab.length}</span>
            </span>
          }
          value={'Custom'}
          data-testid={`rbac-custom-tab`}
        />
      </YBTabs>
      {universeList?.length === 0 && (
        <div className={classes.noUniverses} data-testid={`rbac-no-universe-found`}>
          <BulbIcon />
          {t('noUniverses')}
        </div>
      )}
      <YBTabPanel value={(tab as unknown) as string} tabIndex={'System' as any}>
        {ConnectOnly}
        <Box className={classes.mappingsContainer}>
          {BuiltinRoleTab.length > 0 && (
            <Typography variant="body2" className={classes.title}>
              {t('builtInRoleTab')}
            </Typography>
          )}

          {BuiltinRoleTab.map((p) => p)}
          <div
            className={clsx(classes.assignNewRole, BuiltinRoleTab.length > 0 && classes.marginTop)}
            onClick={() => {
              if (initialMappingValue.roleResourceDefinitions) {
                append([
                  { ...initialMappingValue!.roleResourceDefinitions[0], roleType: 'System' }
                ]);
              }
            }}
            data-testid={`rbac-assign-builtin-role`}
          >
            <i className="fa fa-plus-circle" />
            <Typography variant="body1">{t('assignNewBuiltInRole')}</Typography>
          </div>
        </Box>
      </YBTabPanel>
      <YBTabPanel value={(tab as unknown) as string} tabIndex={'Custom' as any}>
        {ConnectOnly}
        <Box className={classes.mappingsContainer}>
          {CustomRoleTab.length > 0 && (
            <Typography variant="body2" className={classes.title}>
              {t('customRoleTab')}
            </Typography>
          )}
          {CustomRoleTab.map((c) => c)}
          <div
            className={clsx(classes.assignNewRole, CustomRoleTab.length > 0 && classes.marginTop)}
            onClick={() => {
              if (initialMappingValue.roleResourceDefinitions) {
                append([
                  { ...initialMappingValue!.roleResourceDefinitions[0], roleType: 'Custom' }
                ]);
              }
            }}
            data-testid={`rbac-assign-custom-role`}
          >
            <i className="fa fa-plus-circle" />
            <Typography variant="body1">{t('assignNewCustomRole')}</Typography>
          </div>
        </Box>
      </YBTabPanel>
    </div>
  );
};

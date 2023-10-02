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
import { YBButton, YBSelect } from '../../../components';
import { YBLoadingCircleIcon } from '../../../../components/common/indicators';

import { getAllRoles } from '../api';
import { fetchUniversesList } from '../../../../actions/xClusterReplication';
import { Universe } from '../../../helpers/dtos';
import { SelectUniverseResource } from './SelectUniverseResource';
import { RbacUserWithResources } from '../users/interface/Users';
import { initialMappingValue } from '../users/components/CreateUsers';

import { Add } from '@material-ui/icons';
import { ReactComponent as BulbIcon } from '../../../assets/bulb.svg';
import { ReactComponent as Close } from '../../../assets/close copy.svg';
import { YBTabPanel, YBTab, YBTabs } from '../../../components/YBTabs/YBTabs';
import { Role } from '../roles';

const useStyles = makeStyles((theme) => ({
  root: {
    marginTop: theme.spacing(2)
  },
  mappingsContainer: {
    borderRadius: theme.spacing(1),
    border: `1px solid ${theme.palette.ybacolors.backgroundGrayDark}`,
    background: theme.palette.ybacolors.backgroundGrayLightest,
    padding: theme.spacing(2),
    paddingBottom: theme.spacing(3),
    width: '1064px',
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
    width: '490px'
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
    width: '1500px'
  },
  noUniverses: {
    borderRadius: theme.spacing(1),
    border: `1px solid ${theme.palette.ybacolors.backgroundGrayDark}`,
    background: theme.palette.ybacolors.backgroundGrayLight,
    padding: theme.spacing(1.25),
    width: '1067px',
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

  const { control, setValue } = useFormContext<RbacUserWithResources>();
  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.permissions'
  });

  const { fields, append, remove } = useFieldArray({
    control,
    name: 'roleResourceDefinitions'
  });

  const [tab, setTabs] = useState<Role['roleType']>('System');
  const handleChange = (_event: any, newValue: any) => {
    setTabs(newValue);
  };

  if (isRoleListLoading || isUniverseListLoading) return <YBLoadingCircleIcon />;

  if (fields.length === 0) {
    return (
      <>
        <div>
          <Typography variant="body2">{t('userAssignedRole')}</Typography>
        </div>
        <div className={classes.connectOnly}>
          <Trans t={t} i18nKey="connectOnly" components={{ b: <b /> }} />
        </div>

        <YBButton
          startIcon={<Add />}
          size="large"
          variant="primary"
          onClick={() => {
            if (initialMappingValue.roleResourceDefinitions) {
              append(initialMappingValue!.roleResourceDefinitions);
            }
          }}
          data-testid={`rbac-user-assign-role`}
        >
          {t('assignNewRole')}
        </YBButton>
      </>
    );
  }

  const [BuiltinRoleTab, CustomRoleTab] = fields.reduce(
    (prevValue, field, index) => {
      const component = (
        <Box key={field.id} className={classes.roleMargin}>
          <div className={classes.mappingRow}>
            <YBSelect
              name={`mappings.${index}.role`}
              className={classes.roleSelect}
              defaultValue={field.role?.roleUUID}
              onChange={(e) => {
                setValue(
                  `roleResourceDefinitions.${index}.role`,
                  find(roles, { roleUUID: e.target.value })!
                );
              }}
              data-testid={`rbac-role-select`}
            >
              {roles
                ?.filter((r) => r.roleType === tab)
                .map((role) => (
                  <MenuItem
                    key={role.roleUUID}
                    value={role.roleUUID}
                    data-testid={`rbac-role-select-${role.name}`}
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

            <span
              onClick={() => remove(index)}
              className={classes.removeRoleMapping}
              data-testid={`rbac-remove-role`}
            >
              <Close />
            </span>
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
        <Box className={classes.mappingsContainer}>
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
        <Box className={classes.mappingsContainer}>
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

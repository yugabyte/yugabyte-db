/*
 * Created on Wed Aug 02 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
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
    marginTop: theme.spacing(5.25),
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
        >
          {t('assignNewRole')}
        </YBButton>
      </>
    );
  }

  return (
    <div className={classes.root}>
      <Typography variant="body1">{t('title')}</Typography>
      {universeList?.length === 0 && (
        <div className={classes.noUniverses}>
          <BulbIcon />
          {t('noUniverses')}
        </div>
      )}
      <Box className={classes.mappingsContainer}>
        {t('role')}
        {fields.map((field, index) => {
          return (
            <Box key={field.id} className={classes.roleMargin}>
              <div className={classes.mappingRow}>
                <YBSelect
                  name={`mappings.${index}.role`}
                  className={classes.roleSelect}
                  defaultValue={field.roleUUID}
                  onChange={(e) => {
                    setValue(`roleResourceDefinitions.${index}.roleUUID`, e.target.value);
                  }}
                >
                  {roles?.map((role) => (
                    <MenuItem key={role.roleUUID} value={role.roleUUID}>
                      {role.name}
                    </MenuItem>
                  ))}
                </YBSelect>
                {t('for')}
                <SelectUniverseResource universeList={universeList ?? []} fieldIndex={index} />
                <span onClick={() => remove(index)} className={classes.removeRoleMapping}>
                  <Close />
                </span>
              </div>
            </Box>
          );
        })}
        <div
          className={classes.assignNewRole}
          onClick={() => {
            if (initialMappingValue.roleResourceDefinitions) {
              append(initialMappingValue!.roleResourceDefinitions);
            }
          }}
        >
          <i className="fa fa-plus-circle" />
          <Typography variant="body1">{t('assignNewRole')}</Typography>
        </div>
      </Box>
    </div>
  );
};

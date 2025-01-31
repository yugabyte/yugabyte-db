/*
 * Created on Wed Aug 21 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';
import { makeStyles } from '@material-ui/core';
import { components } from 'react-select';
import { api } from '../../../../../helpers/api';
import { GetRestoreContext } from '../../RestoreUtils';

import { hasNecessaryPerm } from '../../../../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../../rbac/ApiAndUserPermMapping';
import { YBTag, YBTag_Types } from '../../../../../../components/common/YBTag';
import { YBReactSelectField } from '../../../../../../components/configRedesign/providerRedesign/components/YBReactSelect/YBReactSelectField';
import {
  ReactSelectComponents,
  ReactSelectStyles
} from '../../../scheduled/create/ReactSelectStyles';
import { YBLoadingCircleIcon } from '../../../../../../components/common/indicators';
import { Universe } from '../../../../../helpers/dtos';
import { RestoreFormModel } from '../../models/RestoreFormModel';

const useStyles = makeStyles((theme) => ({
  sourceUniverseLabel: {
    display: 'inline-flex !important',
    alignItems: 'center',
    '& .status-badge': {
      marginLeft: theme.spacing(0.5)
    }
  },
  title: {
    marginBottom: '8px'
  }
}));

const SelectUniverse = () => {
  const [{ backupDetails }] = GetRestoreContext();

  const {
    control,
    formState: { errors }
  } = useFormContext<RestoreFormModel>();

  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.restore.target'
  });

  const classes = useStyles();

  const reactSelectComp = ReactSelectComponents(!!errors?.target?.targetUniverse?.message);

  const { data: universeList, isLoading: isUniverseListLoading } = useQuery(
    ['universeList'],
    api.fetchUniverseList
  );

  let sourceUniverseNameAtFirst: Universe[] = [];

  // display source universe at top
  if (universeList && universeList.length > 0) {
    sourceUniverseNameAtFirst = [...universeList.filter((u) => u.universeUUID)];
    const sourceUniverseIndex = universeList.findIndex(
      (u) => u.universeUUID === backupDetails?.universeUUID
    );
    if (sourceUniverseIndex) {
      const sourceUniverse = sourceUniverseNameAtFirst.splice(sourceUniverseIndex, 1);
      sourceUniverseNameAtFirst.unshift(sourceUniverse[0]);
    }
    sourceUniverseNameAtFirst = sourceUniverseNameAtFirst.filter(
      (u) => !u.universeDetails.universePaused
    );
  }

  if (isUniverseListLoading) return <YBLoadingCircleIcon />;

  return (
    <div>
      <div className={classes.title}>{t('targetUniverse')}</div>
      <YBReactSelectField
        control={control}
        name="target.targetUniverse"
        width="620px"
        options={sourceUniverseNameAtFirst?.map((universe: Universe) => {
          return {
            label: universe.name,
            value: universe.universeUUID,
            isDisabled: !hasNecessaryPerm({
              onResource: universe.universeUUID,
              ...ApiPermissionMap.UNIVERSE_RESTORE_BACKUP
            })
          };
        })}
        stylesOverride={ReactSelectStyles}
        isClearable
        components={{
          // eslint-disable-next-line react/display-name
          Option: (props: any) => {
            if (props.data.value === backupDetails?.universeUUID) {
              return (
                <components.Option {...props} className={classes.sourceUniverseLabel}>
                  {props.data.label}
                  <YBTag type={YBTag_Types.YB_GRAY}>{t('backupSource')}</YBTag>
                </components.Option>
              );
            }
            return <components.Option {...props} />;
          },
          SingleValue: ({ data }: { data: any }) => {
            if (data.value === backupDetails?.universeUUID) {
              return (
                <>
                  <span>{data.label}</span> &nbsp;
                  <YBTag type={YBTag_Types.YB_GRAY}>{t('backupSource')}</YBTag>
                </>
              );
            }
            return data.label;
          },
          ...reactSelectComp
        }}
      />
    </div>
  );
};

SelectUniverse.displayName = 'SelectUniverse';
export default SelectUniverse;

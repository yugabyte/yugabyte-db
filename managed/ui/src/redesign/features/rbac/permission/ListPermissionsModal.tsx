/*
 * Created on Thu Jul 13 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useMap } from 'react-use';
import { find, flatten, values } from 'lodash';
import { useTranslation } from 'react-i18next';
import { makeStyles } from '@material-ui/core';
import { YBCheckbox, YBModal } from '../../../components';
import { Permission } from './IPermission';
import { getPermissionDisplayText } from '../rbacUtils';

type ListPermissionsModalProps = {
  onHide: () => void;
  onSubmit: (permission: Permission[]) => void;
  visible: boolean;
  permissionsList: Permission[];
  defaultPerm: Permission[];
};

const convertPermissionListToMap = (
  permissionList: Permission[],
  selectedPermissions: Permission[]
) => {
  const permMap: Record<string, Permission> = {};
  selectedPermissions.forEach((sp) => {
    const p = find(permissionList, sp);
    if (p) {
      permMap[getPermissionDisplayText(sp)] = p;
    }
  });
  return permMap;
};

const useStyles = makeStyles((theme) => ({
  permission_container: {
    padding: theme.spacing(2),
    borderRadius: theme.spacing(1),
    border: `1px solid ${theme.palette.grey[200]}`,
    background: theme.palette.common.white
  }
}));

function ListPermissionsModal({
  onHide,
  onSubmit,
  visible,
  permissionsList,
  defaultPerm
}: ListPermissionsModalProps) {
  const [selectedPermissions, { set, get, remove, setAll, reset }] = useMap(
    convertPermissionListToMap(permissionsList, defaultPerm)
  );

  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.permissions.selectPermissionModal'
  });

  const classes = useStyles();

  const dependentPermissions = flatten(
    values(selectedPermissions).map((p: Permission) => {
      return flatten(
        p.prerequisitePermissions.map((s) => {
          const label = getPermissionDisplayText(s);
          if (!get(label)) {
            set(label, find(permissionsList, s) as any);
          }
          return label;
        })
      );
    })
  );

  const selectedPermissionsCount = values(selectedPermissions).length;

  return (
    <YBModal
      title={t('title')}
      open={visible}
      submitLabel={t('confirm', { keyPrefix: 'common' })}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      onSubmit={() => {
        onSubmit(values(selectedPermissions));
        onHide();
      }}
      onClose={onHide}
      overrideHeight={'554px'}
    >
      <div className={classes.permission_container}>
        <div>
          <YBCheckbox
            label={t('selectAll', { keyPrefix: 'common' })}
            indeterminate={
              selectedPermissionsCount > 0 &&
              permissionsList &&
              selectedPermissionsCount < permissionsList.length
            }
            onChange={(_, state) => {
              if (state) {
                if (!permissionsList) return;
                setAll(
                  Object.fromEntries(
                    permissionsList.map((obj) => [getPermissionDisplayText(obj), obj])
                  )
                );
              } else {
                reset();
              }
            }}
            checked={selectedPermissionsCount === permissionsList?.length}
          />
        </div>

        {permissionsList.map((permission, i) => {
          const label = getPermissionDisplayText(permission);
          return (
            <div key={i}>
              <YBCheckbox
                key={i}
                name={`selectedPermissions.${i}`}
                label={label}
                onChange={(_, state) => {
                  if (state) {
                    set(label, permission);
                  } else {
                    remove(label);
                  }
                }}
                checked={!!get(label)}
                disabled={dependentPermissions.includes(label)}
              />
            </div>
          );
        })}
      </div>
    </YBModal>
  );
}

export default ListPermissionsModal;

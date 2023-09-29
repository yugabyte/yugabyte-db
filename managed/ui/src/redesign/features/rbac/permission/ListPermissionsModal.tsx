/*
 * Created on Thu Jul 13 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useMap } from 'react-use';
import { concat, find, flattenDeep, groupBy, isEmpty, values } from 'lodash';
import { useTranslation } from 'react-i18next';
import {
  Accordion,
  AccordionDetails,
  AccordionSummary,
  Typography,
  makeStyles
} from '@material-ui/core';
import { YBCheckbox, YBModal } from '../../../components';
import { Permission, Resource, ResourceType } from './IPermission';
import { resourceOrderByRelevance } from '../common/RbacUtils';
import { ArrowDropDown } from '@material-ui/icons';

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
  const permMap: Record<ResourceType, Permission[]> = {
    UNIVERSE: [],
    OTHER: [],
    ROLE: [],
    USER: []
  };
  selectedPermissions.forEach((sp) => {
    if (isEmpty(sp)) return;
    const p = find(permissionList, sp);
    if (p) {
      permMap[sp.resourceType] = concat(permMap[sp.resourceType], p);
    }
  });
  return permMap;
};

const useStyles = makeStyles((theme) => ({
  permission_container: {
    '& .MuiAccordionSummary-root': {
      background: theme.palette.ybacolors.backgroundGrayLightest,
      padding: '14px 24px',
      height: '45px',
      '&.Mui-expanded': {
        borderBottom: `1px solid ${theme.palette.ybacolors.backgroundGrayDark}`
      }
    },
    '& .MuiAccordionDetails-root': {
      display: 'flex',
      flexDirection: 'column',
      padding: `16px`,
      gap: '8px'
    },
    '& .MuiAccordion-root.Mui-expanded,& .MuiAccordionSummary-root.Mui-expanded': {
      minHeight: 'unset',
      margin: 0
    }
  },
  resourceTitle: {
    display: 'flex',
    justifyContent: 'space-between',
    width: '100%'
  },
  selectedPermCount: {
    padding: '2px 6px',
    borderRadius: '4px',
    background: theme.palette.primary[200],
    color: theme.palette.primary[700]
  },
  permissionGroupTitle: {
    textTransform: 'capitalize'
  },
  readReplica: {
    color: '#67666C',
    marginLeft: '5px'
  },
  expandMore: {
    fontSize: '24px'
  }
}));

function ListPermissionsModal({
  onHide,
  onSubmit,
  visible,
  permissionsList,
  defaultPerm
}: ListPermissionsModalProps) {
  const [selectedPermissions, { set }] = useMap(
    convertPermissionListToMap(permissionsList, defaultPerm)
  );

  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.permissions.selectPermissionModal'
  });

  const classes = useStyles();

  const permissionGroups = groupBy(permissionsList, (perm) => perm.resourceType);

  const dependentPermissions = flattenDeep(
    values(selectedPermissions).map((permissions) => {
      return permissions.map((p: Permission) => {
        return p.prerequisitePermissions.map((s) => {
          const perm = find(permissionGroups[s.resourceType], {
            resourceType: s.resourceType,
            action: s.action
          });
          if (perm && find(selectedPermissions[s.resourceType], perm) === undefined) {
            set(s.resourceType, concat(selectedPermissions[s.resourceType], perm));
          }
          return perm;
        });
      });
    })
  );

  return (
    <YBModal
      title={t('title')}
      open={visible}
      submitLabel={t('confirm', { keyPrefix: 'common' })}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      onSubmit={() => {
        onSubmit(flattenDeep(values(selectedPermissions)));
        onHide();
      }}
      onClose={onHide}
      overrideHeight={'730px'}
      overrideWidth={'800px'}
      style={{
        maxWidth: 'unset',
        maxHeight: 'unset'
      }}
      size="lg"
    >
      <div className={classes.permission_container}>
        {resourceOrderByRelevance.map((resourceType, i) => {
          return (
            <Accordion key={i}>
              <AccordionSummary
                expandIcon={
                  <ArrowDropDown
                    className={classes.expandMore}
                    data-testid={`rbac-resource-${resourceType}`}
                  />
                }
              >
                <div className={classes.resourceTitle}>
                  <Typography variant="body1" className={classes.permissionGroupTitle}>
                    {t('resourceManagement', { resource: resourceType.toLowerCase() })}
                    {resourceType === Resource.UNIVERSE && (
                      <Typography
                        variant="subtitle1"
                        component={'span'}
                        className={classes.readReplica}
                      >
                        {t('universePrimaryAndReplica')}
                      </Typography>
                    )}
                  </Typography>
                  <span className={classes.selectedPermCount}>
                    {t('selectionCount', {
                      count: selectedPermissions[resourceType].length,
                      total: permissionGroups[resourceType].length
                    })}
                  </span>
                </div>
              </AccordionSummary>
              <AccordionDetails>
                <YBCheckbox
                  label={t('selectAllPermissions', { resource: resourceType.toLowerCase() })}
                  indeterminate={
                    selectedPermissions[resourceType].length > 0 &&
                    permissionsList &&
                    selectedPermissions[resourceType].length < permissionGroups[resourceType].length
                  }
                  data-testid={`rbac-resource-${resourceType}-selectAll`}
                  onChange={(_, state) => {
                    if (state) {
                      if (!permissionsList) return;
                      set(resourceType, permissionGroups[resourceType]);
                    } else {
                      set(resourceType, []);
                    }
                  }}
                  checked={
                    selectedPermissions[resourceType].length ===
                    permissionGroups[resourceType].length
                  }
                />
                {permissionGroups[resourceType].map((permission, i) => {
                  return (
                    <YBCheckbox
                      key={i}
                      name={`selectedPermissions.${i}`}
                      label={permission.name}
                      onChange={(_e, state) => {
                        if (state) {
                          set(resourceType, concat(selectedPermissions[resourceType], permission));
                        } else {
                          set(
                            resourceType,
                            selectedPermissions[resourceType].filter(
                              (p) => p.name !== permission.name
                            )
                          );
                        }
                      }}
                      checked={find(selectedPermissions[resourceType], permission) !== undefined}
                      disabled={find(dependentPermissions, permission) !== undefined}
                      data-testid={`rbac-resource-${resourceType}-${permission.name}`}
                    />
                  );
                })}
              </AccordionDetails>
            </Accordion>
          );
        })}
      </div>
    </YBModal>
  );
}

export default ListPermissionsModal;

/*
 * Created on Thu Jul 13 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useEffect, useState } from 'react';
import { useMap } from 'react-use';
import clsx from 'clsx';
import { capitalize, concat, find, flattenDeep, groupBy, isEmpty, keys, values } from 'lodash';
import { useTranslation } from 'react-i18next';
import {
  Accordion,
  AccordionDetails,
  AccordionSummary,
  Popover,
  Typography,
  makeStyles
} from '@material-ui/core';
import { YBCheckbox, YBModal } from '../../../components';
import { isDefinedNotNull } from '../../../../utils/ObjectUtils';
import { permissionOrderByRelevance, resourceOrderByRelevance } from '../common/RbacUtils';
import { Action, Permission, Resource, ResourceType } from './IPermission';
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
    '&:last-child': {
      borderRadius: '0px 0px 8px 8px'
    },
    '&:first-child': {
      borderRadius: '8px 8px 0px 0px'
    },
    '& .MuiAccordionSummary-root': {
      background: theme.palette.ybacolors.backgroundGrayLightest,
      padding: '24px 24px 18px 24px',
      height: '35px',
      borderRadius: theme.spacing(1),
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
      minHeight: '40px',
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
    fontSize: '30px'
  },
  contentRoot: {
    padding: '20px'
  },
  checkbox: {
    transform: 'scale(1.3)'
  },
  insetCheckbox: {
    marginLeft: '24px'
  },
  universeInfoText: {
    textTransform: 'uppercase',
    color: "#67666C",
    marginLeft: '10px'
  }
}));

function ListPermissionsModal({
  onHide,
  onSubmit,
  visible,
  permissionsList,
  defaultPerm
}: ListPermissionsModalProps) {
  const [selectedPermissions, { set, setAll }] = useMap(
    convertPermissionListToMap(permissionsList, defaultPerm)
  );

  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.permissions.selectPermissionModal'
  });

  const classes = useStyles();

  const permissionGroups = groupBy(permissionsList, (perm) => perm.resourceType);

  keys(permissionGroups).forEach((key) => {
    permissionGroups[key] = permissionGroups[key].sort((a, b) => {
      return (
        permissionOrderByRelevance.indexOf(a.action) - permissionOrderByRelevance.indexOf(b.action)
      );
    });
  });

  if (isDefinedNotNull(permissionGroups[Resource.DEFAULT])) {
    permissionGroups[Resource.DEFAULT] = permissionGroups[Resource.DEFAULT].filter(
      (p) => p.action !== Action.SUPER_ADMIN_ACTIONS
    );
  }

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

  useEffect(() => {
    if (!visible) {
      setAll(convertPermissionListToMap(permissionsList, defaultPerm));
    }
  }, [visible, setAll, permissionsList, defaultPerm, convertPermissionListToMap]);

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
      dialogContentProps={{
        className: classes.contentRoot
      }}
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
                    {resourceType === Resource.DEFAULT
                      ? t('otherResource')
                      : t('resourceManagement', { resource: resourceType.toLowerCase() })}
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
                  label={
                    resourceType === Resource.DEFAULT
                      ? t('selectAllOtherPermissions')
                      : t('selectAllPermissions', {
                          resource: capitalize(resourceType.toLowerCase())
                        })
                  }
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
                  className={classes.checkbox}
                />
                {resourceType === Resource.UNIVERSE && (
                  <Typography variant="subtitle1" className={classes.universeInfoText}>
                    {t('universePrimaryAndReplica2')}
                  </Typography>
                )}
                {permissionGroups[resourceType].map((permission, i) => {
                  if (permission.action === Action.SUPER_ADMIN_ACTIONS) return null; // we cannot assign super-admin action
                  const comp = (
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
                      className={clsx(classes.checkbox, classes.insetCheckbox)}
                    />
                  );
                  if (find(dependentPermissions, permission) !== undefined) {
                    return (
                      <DisabledCheckbox hoverMsg={t('disabledDependentPerm')}>
                        {comp}
                      </DisabledCheckbox>
                    );
                  }
                  return comp;
                })}
              </AccordionDetails>
            </Accordion>
          );
        })}
      </div>
    </YBModal>
  );
}

const disabledPopoverStyles = makeStyles((theme) => ({
  popover: {
    pointerEvents: 'none'
  },
  root: {
    padding: theme.spacing(1),
    width: '210px',
    color: '#67666C'
  }
}));

const DisabledCheckbox = ({ children, hoverMsg }: { children: JSX.Element; hoverMsg: string }) => {
  const [anchorEl, setAnchorEl] = useState<HTMLElement | null>(null);

  const classes = disabledPopoverStyles();

  const handlePopoverOpen = (event: React.MouseEvent<HTMLElement>) => {
    setAnchorEl(event.currentTarget);
  };

  const handlePopoverClose = () => {
    setAnchorEl(null);
  };

  const open = Boolean(anchorEl);

  return (
    <div onMouseEnter={handlePopoverOpen} onMouseLeave={handlePopoverClose}>
      {children}
      <Popover
        id="dependent-perm-disabled"
        className={classes.popover}
        classes={{
          paper: classes.root
        }}
        open={open}
        anchorEl={anchorEl}
        anchorOrigin={{
          vertical: 'top',
          horizontal: 'left'
        }}
        transformOrigin={{
          vertical: 'bottom',
          horizontal: 'left'
        }}
        onClose={handlePopoverClose}
        disableRestoreFocus
      >
        <Typography variant="subtitle1">{hoverMsg}</Typography>
      </Popover>
    </div>
  );
};

export default ListPermissionsModal;

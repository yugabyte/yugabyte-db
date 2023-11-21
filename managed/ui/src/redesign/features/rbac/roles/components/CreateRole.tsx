/*
 * Created on Wed Jul 12 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useContext, useImperativeHandle, useRef } from 'react';
import { useForm } from 'react-hook-form';
import { useMutation, useQuery } from 'react-query';
import { toast } from 'react-toastify';
import { find, groupBy, isEmpty } from 'lodash';
import { useToggle } from 'react-use';
import { useTranslation } from 'react-i18next';
import {
  Accordion,
  AccordionDetails,
  AccordionSummary,
  Box,
  FormHelperText,
  Typography,
  makeStyles
} from '@material-ui/core';
import Container from '../../common/Container';
import ListPermissionsModal from '../../permission/ListPermissionsModal';
import { YBButton, YBInputField } from '../../../../components';
import { YBLoadingCircleIcon } from '../../../../../components/common/indicators';
import { resourceOrderByRelevance } from '../../common/RbacUtils';
import { Role, RoleType } from '../IRoles';
import { Permission, Resource } from '../../permission';
import { createRole, editRole, getAllAvailablePermissions } from '../../api';
import { Pages, RoleContextMethods, RoleViewContext } from '../RoleContext';
import { createErrorMessage } from '../../../universe/universe-form/utils/helpers';
import { isDefinedNotNull, isNonEmptyString } from '../../../../../utils/ObjectUtils';
import { ArrowDropDown, Create } from '@material-ui/icons';
import { yupResolver } from '@hookform/resolvers/yup';
import { getRoleValidationSchema } from '../RoleValidationSchema';

const PERMISSION_MODAL_TRANSLATION_PREFIX = 'rbac.permissions.selectPermissionModal';

const useStyles = makeStyles((theme) => ({
  root: {
    padding: theme.spacing(4),
    width: '764px',
    minHeight: '350px'
  },
  title: {
    fontSize: '17px',
    marginBottom: theme.spacing(3)
  },
  form: {
    '&>div': {
      marginBottom: theme.spacing(3)
    },
    '& .MuiInputLabel-root': {
      textTransform: 'unset',
      fontSize: '13px',
      marginBottom: theme.spacing(0.8),
      fontWeight: 400,
      color: '#333'
    }
  },
  permissionTitle: {
    marginTop: '18px'
  }
}));

export const CreateRoleWithContainer = () => {
  const createRoleRef = useRef<any>(null);

  return (
    <Container
      onCancel={() => {
        createRoleRef.current?.onCancel();
      }}
      onSave={() => {
        createRoleRef.current?.onSave();
      }}
    >
      <CreateRole ref={createRoleRef} />
    </Container>
  );
};
// eslint-disable-next-line react/display-name
export const CreateRole = forwardRef((_, forwardRef) => {
  const classes = useStyles();
  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.roles.create'
  });

  const [{ currentRole }, { setCurrentPage }] = (useContext(
    RoleViewContext
  ) as unknown) as RoleContextMethods;

  const {
    control,
    setValue,
    handleSubmit,
    watch,
    formState: { errors }
  } = useForm<Role>({
    defaultValues: currentRole
      ? {
          ...currentRole,
          permissionDetails: currentRole.permissionDetails
        }
      : {
          description: '',
          permissionDetails: {
            permissionList: []
          }
        },
    resolver: yupResolver(getRoleValidationSchema(t))
  });

  const doCreateRole = useMutation(
    (role: Role) => {
      return createRole(role);
    },
    {
      onSuccess: (_resp, role) => {
        toast.success(t('successMsg', { role_name: role.name }));
        setCurrentPage(Pages.LIST_ROLE);
      },
      onError: (err) => {
        toast.error(createErrorMessage(err));
      }
    }
  );

  const doEditRole = useMutation(
    (role: Role) => {
      return editRole(role);
    },
    {
      onSuccess: (_resp, role) => {
        toast.success(t('editSuccessMsg', { role_name: role.name }));
        setCurrentPage(Pages.LIST_ROLE);
      },
      onError: (err) => {
        toast.error(createErrorMessage(err));
      }
    }
  );

  const onSave = () => {
    handleSubmit((val) => {
      if (!currentRole?.roleUUID) {
        doCreateRole.mutate(val);
      } else {
        doEditRole.mutate(val);
      }
    })();
  };

  const onCancel = () => {
    setCurrentPage(Pages.LIST_ROLE);
  };

  useImperativeHandle(
    forwardRef,
    () => ({
      onSave,
      onCancel
    }),
    [onSave, onCancel]
  );

  const permissionListVal = watch('permissionDetails.permissionList');

  const isSystemRole = currentRole?.roleType === RoleType.SYSTEM;

  return (
    <Box className={classes.root}>
      <div className={classes.title}>{t(currentRole?.roleUUID ? 'edit' : 'title')}</div>
      <form className={classes.form}>
        <YBInputField
          name="name"
          control={control}
          label={t('form.name')}
          placeholder={t('form.namePlaceholder')}
          fullWidth
          disabled={isNonEmptyString(currentRole?.roleUUID) || isSystemRole}
        />
        <YBInputField
          name="description"
          control={control}
          label={t('form.description')}
          placeholder={t('form.descriptionPlaceholder')}
          disabled={isNonEmptyString(currentRole?.roleUUID) || isSystemRole}
          fullWidth
        />
        {permissionListVal.length === 0 && (
          <Typography variant="body1" className={classes.permissionTitle} component={'div'}>
            {t('form.permissions')}
          </Typography>
        )}
        <SelectPermissions
          selectedPermissions={permissionListVal}
          setSelectedPermissions={(perm: Permission[]) => {
            setValue('permissionDetails.permissionList', perm);
          }}
          disabled={isNonEmptyString(currentRole?.roleUUID) && isSystemRole}
        />
        {errors.permissionDetails?.message && (
          <FormHelperText required error>
            {errors.permissionDetails.message}
          </FormHelperText>
        )}
      </form>
    </Box>
  );
});

const permissionsStyles = makeStyles((theme) => ({
  root: {
    width: '700px',
    borderRadius: theme.spacing(1),
    border: `1px dashed ${theme.palette.primary[300]}`,
    background: theme.palette.primary[100],
    height: '120px',
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2),
    alignItems: 'center',
    justifyContent: 'center'
  },
  helpText: {
    fontFamily: 'Inter',
    fontWeight: 400,
    lineHeight: `${theme.spacing(2)}px`,
    fontSize: '11.5px',
    color: '#67666C'
  },
  permList: {
    width: '100%'
  },
  header: {
    display: 'flex',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: '16px',
    '& > p': {
      fontSize: '16px'
    }
  },
  selectionCount: {
    padding: '2px 6px',
    borderRadius: '4px',
    background: theme.palette.primary[200],
    color: theme.palette.primary[700]
  },
  divider: {
    marginLeft: theme.spacing(1),
    marginRight: theme.spacing(2)
  },
  editSelection: {
    fontSize: '12px',
    height: '30px',
    '& .MuiButton-label': {
      fontSize: '12px'
    },
    '& svg': {
      fontSize: '14px !important'
    }
  },
  permCollection: {
    '&:last-child': {
      borderRadius: '0px 0px 8px 8px'
    },
    '&:first-child': {
      borderRadius: '8px 8px 0px 0px'
    },
    '& .MuiAccordionSummary-root': {
      background: theme.palette.ybacolors.backgroundGrayLightest,
      padding: '24px 24px 24px 24px',
      height: '35px',
      borderRadius: theme.spacing(1),
      '&.Mui-expanded': {
        borderBottom: `1px solid ${theme.palette.ybacolors.backgroundGrayDark}`
      }
    },
    '& .MuiAccordionDetails-root': {
      display: 'flex',
      flexDirection: 'column',
      padding: `24px`,
      gap: '8px'
    },
    '& .MuiAccordion-root.Mui-expanded,& .MuiAccordionSummary-root.Mui-expanded': {
      minHeight: '40px',
      margin: 0
    }
  },
  permItem: {
    marginBottom: theme.spacing(2)
  },
  expandMore: {
    fontSize: '24px'
  },
  resourceTitle: {
    display: 'flex',
    justifyContent: 'space-between',
    width: '100%'
  },
  permissionGroupTitle: {
    textTransform: 'capitalize'
  },
  readReplica: {
    color: '#67666C',
    marginLeft: '5px'
  },
  selectedPermCount: {
    padding: '2px 6px',
    borderRadius: '4px',
    background: theme.palette.primary[200],
    color: theme.palette.primary[700]
  },
  universeInfoText: {
    textTransform: 'uppercase',
    color: theme.palette.ybacolors.textDarkGray,
    marginBottom: '10px'
  }
}));

type SelectPermissionsProps = {
  selectedPermissions: Permission[];
  setSelectedPermissions: (permissions: Permission[]) => void;
  disabled: boolean;
};

const SelectPermissions = ({
  selectedPermissions,
  setSelectedPermissions,
  disabled
}: SelectPermissionsProps) => {
  const classes = permissionsStyles();
  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.roles.create.form'
  });

  const [permissionModalVisible, togglePermissionModal] = useToggle(false);

  const { isLoading, data: availablePermissions } = useQuery(
    'permissions',
    () => getAllAvailablePermissions(),
    {
      select: (data) => data.data
    }
  );

  if (isLoading) return <YBLoadingCircleIcon />;

  const getEmptyList = () => (
    <Box className={classes.root}>
      <YBButton variant="secondary" onClick={() => togglePermissionModal(true)} disabled={disabled}>
        {t('selectPermissions')}
      </YBButton>
      <div className={classes.helpText}>{t('selectPermissionSubText')}</div>
    </Box>
  );

  const listPermissions = () => {
    if (!availablePermissions) return <YBLoadingCircleIcon />;

    const permissions = availablePermissions.filter((p) =>
      find(selectedPermissions, { action: p.action, resourceType: p.resourceType })
    );
    const permissionGroups = groupBy(permissions, (perm) => perm.resourceType);
    return (
      <div className={classes.permList}>
        <div className={classes.header}>
          <Typography variant="body1">{t('permissions')}</Typography>
          <YBButton
            variant="secondary"
            className={classes.editSelection}
            startIcon={<Create />}
            onClick={() => togglePermissionModal(true)}
            data-testid={`rbac-edit-universe-selection`}
            disabled={disabled}
          >
            {t('editSelection')}
          </YBButton>
        </div>
        <div className={classes.permCollection}>
          {resourceOrderByRelevance.map((resourceType, i) => {
            if (!isDefinedNotNull(permissionGroups[resourceType])) return null;
            return (
              <Accordion key={i}>
                <AccordionSummary
                  expandIcon={<ArrowDropDown className={classes.expandMore} />}
                  data-testid={`rbac-resource-${resourceType}`}
                >
                  <div className={classes.resourceTitle}>
                    <Typography variant="body1" className={classes.permissionGroupTitle}>
                      {resourceType === Resource.DEFAULT
                        ? t('otherResource', {
                            keyPrefix: PERMISSION_MODAL_TRANSLATION_PREFIX
                          })
                        : t('resourceManagement', {
                            resource: resourceType.toLowerCase(),
                            keyPrefix: PERMISSION_MODAL_TRANSLATION_PREFIX
                          })}
                      {resourceType === Resource.UNIVERSE && (
                        <Typography
                          variant="subtitle1"
                          component={'span'}
                          className={classes.readReplica}
                        >
                          {t('universePrimaryAndReplica', {
                            keyPrefix: PERMISSION_MODAL_TRANSLATION_PREFIX
                          })}
                        </Typography>
                      )}
                    </Typography>
                    <span
                      className={classes.selectedPermCount}
                      data-testid={`rbac-resource-${resourceType}-count`}
                    >
                      {t('permissionsCount', { count: permissionGroups[resourceType].length })}
                    </span>
                  </div>
                </AccordionSummary>
                <AccordionDetails>
                  {resourceType === Resource.UNIVERSE && (
                    <Typography variant="subtitle1" className={classes.universeInfoText}>
                      {t('universePrimaryAndReplica2', {
                        keyPrefix: PERMISSION_MODAL_TRANSLATION_PREFIX
                      })}
                    </Typography>
                  )}
                  {permissionGroups[resourceType].map((permission, i) => {
                    return (
                      <div
                        key={i}
                        className={classes.permItem}
                        data-testid={`rbac-resource-${resourceType}-${permission.name}`}
                      >
                        {permission.name}
                      </div>
                    );
                  })}
                </AccordionDetails>
              </Accordion>
            );
          })}
        </div>
      </div>
    );
  };

  return (
    <>
      {isEmpty(selectedPermissions) ? getEmptyList() : listPermissions()}
      <ListPermissionsModal
        visible={permissionModalVisible}
        onHide={() => togglePermissionModal(false)}
        onSubmit={setSelectedPermissions}
        permissionsList={availablePermissions ?? []}
        defaultPerm={selectedPermissions}
      />
    </>
  );
};

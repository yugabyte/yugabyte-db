/*
 * Created on Mon Jul 08 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { useQuery } from 'react-query';
import { useToggle } from 'react-use';
import { find, omit } from 'lodash';
import { toast } from 'react-toastify';
import { yupResolver } from '@hookform/resolvers/yup';
import { FormProvider, useForm } from 'react-hook-form';
import { Trans, useTranslation } from 'react-i18next';

import {
  Box,
  FormControlLabel,
  FormHelperText,
  FormLabel,
  RadioGroup,
  Typography,
  makeStyles
} from '@material-ui/core';
import Container from '../../common/Container';
import { AlertVariant, YBAlert, YBButton, YBInputField, YBRadio } from '../../../../components';
import { YBErrorIndicator, YBLoadingCircleIcon } from '../../../../../components/common/indicators';
import { DeleteGroupModal } from './DeleteGroup';
import { RolesAndResourceMapping } from '../../policy';
import { RbacUserWithResources } from '../../users/interface/Users';
import {
  GetGroupContext,
  getIsLDAPEnabled,
  getIsOIDCEnabled,
  OIDC_RUNTIME_CONFIGS_QUERY_KEY,
  WrapDisabledElements
} from './GroupUtils';
import { mapResourceBindingsToApi } from '../../rbacUtils';
import { api } from '../../../universe/universe-form/utils/api';
import { isRbacEnabled } from '../../common/RbacUtils';
import { createErrorMessage } from '../../../universe/universe-form/utils/helpers';
import { useUpdateGroupMappings } from '../../../../../v2/api/authentication/authentication';

import { getAllRoles } from '../../api';
import { getGroupValidationSchema, getRoleResourceValidationSchema } from './GroupValidationSchema';
import { Role } from '../../roles';
import { Pages } from './GroupContext';
import {
  AuthGroupToRolesMapping,
  AuthGroupToRolesMappingType
} from '../../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { ReactComponent as AnnouncementIcon } from '../../../../assets/announcement.svg';
import { ReactComponent as ArrowLeft } from '../../../../assets/arrow_left.svg';
import { ReactComponent as Delete } from '../../../../assets/trashbin.svg';

const useStyles = makeStyles((theme) => ({
  root: {
    height: '100%',
    padding: '34px 30px',
    '&>*': {
      marginBottom: '24px'
    },
    '& .MuiFormLabel-root': {
      textTransform: 'capitalize',
      marginBottom: '16px',
      fontSize: '13px',
      fontWeight: 400,
      color: '#151730'
    }
  },
  groupName: {
    width: '800px'
  },
  authProviderOptions: {
    flexDirection: 'row',
    margin: 0
  },
  authProviderMainLabel: {
    ...theme.typography.subtitle2,
    fontWeight: theme.typography.fontWeightMedium as number,
    fontSize: theme.typography.subtitle1.fontSize,
    marginBottom: theme.spacing(0.5),
    marginTop: theme.spacing(0.5)
  },
  authProviderOptionLabel: {
    ...theme.typography.body2,
    marginLeft: theme.spacing(0.5)
  },
  link: {
    color: 'inherit',
    textDecoration: 'underline'
  },
  configureLDAPRoleSettings: {
    width: '800px !important'
  },
  groupInactive: {
    marginBottom: '32px',
    marginTop: '-8px'
  },
  header: {
    borderBottom: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    height: '75px',
    display: 'flex',
    justifyContent: 'space-between',
    alignItems: 'center',
    fontSize: '20px',
    fontWeight: 700,
    padding: '14px'
  },
  back: {
    display: 'flex',
    alignItems: 'center',
    '& svg': {
      width: theme.spacing(4),
      height: theme.spacing(4),
      marginRight: theme.spacing(2),
      cursor: 'pointer'
    }
  }
}));

const convertGroupMappingSpecToRoleResourceDefinition = (
  roleDef: AuthGroupToRolesMapping['role_resource_definitions'],
  roles: Role[]
) => {
  return roleDef?.map((roleResourceDefinition) => {
    const role = find(roles, { roleUUID: roleResourceDefinition.role_uuid }) ?? null;
    return {
      role: role,
      roleType: role?.roleType,
      resourceGroup: roleResourceDefinition.resource_group && {
        resourceDefinitionSet: roleResourceDefinition.resource_group.resource_definition_set.map(
          (resourceDefinition) => {
            return {
              resourceType: resourceDefinition.resource_type,
              allowAll: resourceDefinition.allow_all,
              resourceUUIDSet: resourceDefinition.resource_uuid_set?.map(
                (resourceUUID) => resourceUUID
              )
            };
          }
        )
      }
    };
  });
};

// eslint-disable-next-line react/display-name
export const CreateEditGroup: FC = (_) => {
  const [{ currentGroup }, { setCurrentPage, setCurrentGroup }] = GetGroupContext();

  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.groups.create'
  });

  const { isLoading: isRolesLoading, data: roles, isError: isRolesFailed } = useQuery(
    'roles',
    getAllRoles,
    {
      select: (data) => data.data
    }
  );

  const rbacResourceMethods = useForm<RbacUserWithResources>({
    defaultValues: {
      roleResourceDefinitions: currentGroup
        ? (convertGroupMappingSpecToRoleResourceDefinition(
            currentGroup.role_resource_definitions,
            roles ?? []
          ) as any)
        : []
    },
    resolver: yupResolver(getRoleResourceValidationSchema(t))
  });

  const {
    control,
    getValues,
    setValue,
    watch,
    handleSubmit,
    formState: { errors: groupFormErrors }
  } = useForm<AuthGroupToRolesMapping>({
    defaultValues: {
      group_identifier: '',
      ...(currentGroup ? currentGroup : {})
    },
    resolver: yupResolver(getGroupValidationSchema(t))
  });

  const { data: runtimeConfig, isLoading, isError } = useQuery(OIDC_RUNTIME_CONFIGS_QUERY_KEY, () =>
    api.fetchRunTimeConfigs(true)
  );

  const doAddGroup = useUpdateGroupMappings({
    mutation: {
      onSuccess: () => {
        toast.success(t(!isEditMode ? 'createSuccess' : 'updateSuccess'));
        setCurrentGroup(null);
        setCurrentPage(Pages.LIST_GROUP);
      },
      onError: (resp) => {
        toast.error(createErrorMessage(resp));
      }
    }
  });

  const addGroup = () => {
    const mapping = rbacResourceMethods.getValues();
    const resourceDef = mapResourceBindingsToApi(mapping);
    doAddGroup.mutate({
      data: [
        {
          uuid: isEditMode ? currentGroup?.uuid : undefined,
          ...omit(getValues(), 'creation_date'),
          role_resource_definitions:
            resourceDef?.map((resource) => ({
              role_uuid: resource.roleUUID!,
              ...(resource.resourceGroup && {
                resource_group: {
                  resource_definition_set: resource.resourceGroup.resourceDefinitionSet.map(
                    (resource) => ({
                      resource_type: resource.resourceType,
                      allow_all: resource.allowAll,
                      resource_uuid_set: resource.resourceUUIDSet.map((universe) => universe)
                    })
                  )
                }
              })
            })) ?? []
        }
      ]
    });
  };

  const classes = useStyles();
  const [showDeleteModal, toggleDeleteModal] = useToggle(false);
  const rbacEnabled = isRbacEnabled();
  const onSave = () => {
    handleSubmit(() => {
      rbacResourceMethods.handleSubmit(addGroup)();
    })();
  };

  const onCancel = () => {
    setCurrentGroup(null);
    setCurrentPage(Pages.LIST_GROUP);
  };

  if (isLoading || !runtimeConfig || isRolesLoading) return <YBLoadingCircleIcon />;

  if (isError || isRolesFailed) return <YBErrorIndicator />;

  const isLDAPEnabled = getIsLDAPEnabled(runtimeConfig);
  const isOIDCEnabled = getIsOIDCEnabled(runtimeConfig);

  const isEditMode = currentGroup !== null;

  const authProviderOptions = [
    {
      label: AuthGroupToRolesMappingType.LDAP,
      value: AuthGroupToRolesMappingType.LDAP,
      disabled:
        !isLDAPEnabled || (isEditMode && currentGroup?.type !== AuthGroupToRolesMappingType.LDAP)
    },
    {
      label: AuthGroupToRolesMappingType.OIDC,
      value: AuthGroupToRolesMappingType.OIDC,
      disabled:
        !isOIDCEnabled || (isEditMode && currentGroup?.type !== AuthGroupToRolesMappingType.OIDC)
    }
  ];

  const { errors: roleMappingErros } = rbacResourceMethods.formState;

  const authType = watch('type');

  return (
    <Container
      onCancel={() => {
        onCancel();
      }}
      onSave={() => {
        onSave();
      }}
    >
      <div>
        {isEditMode && (
          <div className={classes.header}>
            <div className={classes.back}>
              <ArrowLeft
                onClick={() => {
                  setCurrentGroup(null);
                  setCurrentPage(Pages.LIST_GROUP);
                }}
              />
              {t('editGroup')}
            </div>
            <YBButton
              variant="secondary"
              size="large"
              data-testid={`rbac-resource-delete-group`}
              startIcon={<Delete />}
              onClick={() => {
                toggleDeleteModal(true);
              }}
            >
              {t('title', { keyPrefix: 'rbac.groups.delete' })}
            </YBButton>
          </div>
        )}
        <Box className={classes.root}>
          {!isEditMode && <Typography variant="h4">{t('title')}</Typography>}
          <div>
            {(authType === AuthGroupToRolesMappingType.LDAP && !isLDAPEnabled) ||
            (authType === AuthGroupToRolesMappingType.OIDC && !isOIDCEnabled) ? (
              <YBAlert
                variant={AlertVariant.Warning}
                icon={<AnnouncementIcon />}
                open
                className={classes.groupInactive}
                text={
                  <Trans
                    t={t}
                    i18nKey="groupInactive"
                    values={{
                      auth_provider: authType
                    }}
                    components={{
                      b: <b />
                    }}
                  />
                }
              />
            ) : null}

            <FormLabel className={classes.authProviderMainLabel}>{t('authProvider')}</FormLabel>
            <RadioGroup className={classes.authProviderOptions}>
              {authProviderOptions.map((option) => {
                return WrapDisabledElements(
                  <FormControlLabel
                    key={`form-radio-option-${option.value}`}
                    value={option.value}
                    control={
                      <YBRadio
                        inputProps={{ 'data-testid': `YBRadio-${option.value}` }}
                        checked={authType === option.value}
                        data-testid={`authType-YBRadio-${option.value}`}
                      />
                    }
                    name="type"
                    label={option.label}
                    disabled={option.disabled}
                    classes={{ label: classes.authProviderOptionLabel }}
                    checked={authType === option.value}
                    onChange={(e: any) => {
                      setValue('type', e.target.value);
                    }}
                  />,
                  option.disabled && !isEditMode,
                  t('authProviderDisabled', { auth_provider: option.value })
                );
              })}
            </RadioGroup>
            {groupFormErrors.type?.message && (
              <FormHelperText required error>
                {groupFormErrors.type.message}
              </FormHelperText>
            )}
          </div>
          {/* <YBAlert
                    variant={AlertVariant.Warning}
                    icon={<AnnouncementIcon />}
                    open
                    className={classes.configureLDAPRoleSettings}
                    text={
                        <Trans
                            t={t}
                            i18nKey="configureLDAPRoleSettings"
                            components={{
                                a: (
                                    <a
                                        className={classes.link}
                                        href="/admin/rbac?tab=user-auth"
                                        rel="noreferrer"
                                        target="_blank"
                                    ></a>
                                ),
                                b: <b />
                            }}
                        />
                    }
                /> */}
          <YBInputField
            name="group_identifier"
            control={control}
            label={t('groupName')}
            className={classes.groupName}
            placeholder={t('groupNamePlaceholder')}
            disabled={isEditMode}
            data-testid="group-identifier"
          />
          <FormProvider {...rbacResourceMethods}>
            <RolesAndResourceMapping
              customTitle={t('groupRolesTitle')}
              hideCustomRoles={!rbacEnabled}
            />
            {roleMappingErros.roleResourceDefinitions?.message && (
              <FormHelperText required error>
                {roleMappingErros.roleResourceDefinitions.message}
              </FormHelperText>
            )}
          </FormProvider>
        </Box>
        <DeleteGroupModal
          open={showDeleteModal}
          onHide={() => {
            toggleDeleteModal(false);
          }}
        />
      </div>
    </Container>
  );
};

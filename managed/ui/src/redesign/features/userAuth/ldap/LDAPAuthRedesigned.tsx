/*
 * Created on Thu Aug 01 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useState } from 'react';
import { find, isString } from 'lodash';
import { useForm } from 'react-hook-form';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { Trans, useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { useToggle } from 'react-use';
import { AxiosResponse } from 'axios';
import { yupResolver } from '@hookform/resolvers/yup';
import {
  Accordion,
  AccordionDetails,
  AccordionSummary,
  makeStyles,
  MenuItem,
  Typography
} from '@material-ui/core';
import { api } from '../../universe/universe-form/utils/api';
import {
  AlertVariant,
  YBAlert,
  YBButton,
  YBInputField,
  YBRadioGroupField,
  YBSelectField,
  YBToggleField,
  YBTooltip
} from '../../../components';
import { YBErrorIndicator, YBLoadingCircleIcon } from '../../../../components/common/indicators';
import { RunTimeConfigEntry } from '../../universe/universe-form/utils/dto';
import { isRbacEnabled } from '../../rbac/common/RbacUtils';
import { RbacValidator } from '../../rbac/common/RbacApiPermValidator';
import { Action } from '../../rbac';
import {
  AuthModes,
  LDAP_RUNTIME_CONFIGS_QUERY_KEY,
  LDAPFormProps,
  LDAPPath,
  LDAPScopes,
  LDAPSecurityOptions,
  LDAPUseQuery,
  LDAPUseQueryOptions,
  SecurityOption,
  TLSVersions
} from './LDAPConstants';
import { escapeStr, TOAST_OPTIONS, UserDefaultRoleOptions } from '../UserAuthUtils';
import { DisableAuthProviderModal } from '../DisableAuthProvider';
import { transformData } from './LDAPUtils';
import { getLDAPValidationSchema } from './LDAPValidationSchema';
import { ArrowDropDown } from '@material-ui/icons';
import { ReactComponent as User } from '../../../../redesign/assets/user-outline.svg';
import { ReactComponent as UserGroupsIcon } from '../../../../redesign/assets/user-group.svg';
import { ReactComponent as BulbIcon } from '../../../../redesign/assets/bulb.svg';

const useStyles = makeStyles((theme) => ({
  root: {
    width: '680px',
    padding: '24px',
    '& .MuiFormLabel-root': {
      textTransform: 'capitalize',
      color: '#333',
      fontSize: '13px',
      fontWeight: 400,
      marginBottom: '6px'
    }
  },
  header: {
    display: 'flex',
    justifyContent: 'space-between'
  },
  configurations: {
    padding: '24px',
    border: `1px solid ${theme.palette.ybacolors.backgroundGrayDark}`,
    borderRadius: '8px',
    marginTop: '16px',
    display: 'flex',
    gap: '24px',
    flexDirection: 'column'
  },
  roleHeader: {
    marginTop: '56px',
    marginBottom: '16px'
  },
  roleAndGroupConfigurations: {
    border: `1px solid ${theme.palette.ybacolors.backgroundGrayDark}`,
    borderRadius: `8px`
  },
  roleConfigurations: {
    padding: '16px 24px',
    height: '86px'
  },
  roleSettingsHeader: {
    display: 'flex',
    gap: '8px',
    alignItems: 'center'
  },
  roleSettings: {
    display: 'flex',
    gap: '56px',
    marginTop: '8px',
    alignItems: 'center',
    marginBottom: '32px'
  },
  alert: {
    marginTop: '8px',
    marginBottom: '16px',
    background: theme.palette.primary[200]
  },
  link: {
    color: 'inherit',
    textDecoration: 'underline'
  },
  ldapProviderConfig: {
    width: '220px',
    padding: '5px 10px',
    '&>span': {
      fontSize: '13px'
    }
  },
  actions: {
    marginTop: '38px',
    display: 'flex',
    justifyContent: 'flex-end',
    gap: '12px'
  },
  infoIcon: {
    color: '#B3B2B5',
    marginLeft: '4px'
  },
  ldapEnabled: {
    display: 'flex',
    alignItems: 'center',
    gap: '4px',
    '& .MuiFormControlLabel-root': {
      marginRight: '0 !important'
    }
  },
  roleField: {
    display: 'flex',
    alignItems: 'center',
    marginLeft: '-12px'
  },
  serviceAccountHeader: {
    marginTop: '32px'
  },
  groupSettings: {
    borderBottom: 'none',
    borderLeft: 'none',
    borderRight: 'none',
    justifyContent: 'center',
    '& .MuiAccordionSummary-root': {
      height: '86px',
      padding: '16px 24px'
    },
    '& .MuiAccordionSummary-content': {
      flexDirection: 'column'
    }
  },
  groupHeader: {
    display: 'flex',
    gap: '8px',
    alignItems: 'center',
    marginBottom: '8px'
  },
  groupsIcon: {
    width: '24px',
    height: '24px'
  },
  accordionDetails: {
    flexDirection: 'column',
    gap: '24px',
    padding: '24px',
    cursor: 'pointer'
  },
  scope: {
    width: '308px'
  }
}));

// The function filters the LDAP configs from the config entries and
// reduces the LDAP configs to form values.

const initializeFormValues = (configEntries: RunTimeConfigEntry[]) => {
  const ldapConfigs = configEntries.filter((config) => config.key.includes(LDAPPath));
  const formData = ldapConfigs.reduce((fData, config) => {
    const [, key] = config.key.split(`${LDAPPath}.`);
    fData[key] = escapeStr(config.value);
    return fData;
  }, {} as any);

  let finalFormData = {
    ...formData,
    use_search_and_bind: formData.use_search_and_bind ?? false,
    ldap_url: formData.ldap_url ? [formData.ldap_url, formData.ldap_port].join(':') : '',
    ldap_group_use_role_mapping: formData.ldap_group_use_role_mapping === 'true',
    use_service_account: !!formData.ldap_service_account_distinguished_name,
    use_ldap: formData.use_ldap === 'true'
  };

  //transform security data
  const { enable_ldaps, enable_ldap_start_tls } = formData;
  const ldap_security =
    enable_ldaps === 'true'
      ? SecurityOption.ENABLE_LDAPS
      : enable_ldap_start_tls === 'true'
      ? SecurityOption.ENABLE_LDAP_START_TLS
      : SecurityOption.UNSECURE;
  finalFormData = { ...finalFormData, ldap_security };

  return finalFormData;
};

export const LDAPAuthNew = () => {
  const classes = useStyles();

  const { t } = useTranslation('translation', {
    keyPrefix: 'userAuth.LDAP'
  });

  const { control, setValue, watch, getValues, handleSubmit } = useForm<LDAPFormProps>({
    resolver: yupResolver(getLDAPValidationSchema(t))
  });

  const { mutateAsync: setRunTimeConfig } = useMutation(
    ({ key, value }: { key: string; value: unknown }) => {
      return api.setRunTimeConfig({ key, value });
    },
    {
      onError(_error, variables) {
        toast.error(t('messages.ldapSaveFailed', { key: variables }), TOAST_OPTIONS);
      }
    }
  );

  const { mutateAsync: deleteRunTimeConfig } = useMutation(
    ({ key }: { key: string }) => {
      return api.deleteRunTimeConfig({ key });
    },
    {
      onError(_error, variables) {
        toast.error(t('messages.ldapSaveFailed', { key: variables }), TOAST_OPTIONS);
      }
    }
  );

  const [showldapDisableModal, toggleldapDisableModal] = useToggle(false);
  const [initialData, setInitialData] = useState<LDAPFormProps>();
  const [groupSettingsExpanded, setGroupSettingsExpanded] = useToggle(true);
  const queryClient = useQueryClient();

  const { isLoading, isError } = useQuery(
    [LDAP_RUNTIME_CONFIGS_QUERY_KEY],
    () => api.fetchRunTimeConfigs(true),
    {
      onSuccess(data) {
        const formData = initializeFormValues(data.configEntries);
        setInitialData(formData);
        Object.entries(formData).forEach(([key, value]) => {
          setValue((key as unknown) as keyof LDAPFormProps, value as any, {
            shouldValidate: false
          });
        });
      }
    }
  );

  // It compares the initial data with the current data and saves the changes.
  // If the value is empty, it deletes the config entry.
  // The function returns an array of promises that are resolved when the configs are saved.
  const saveLDAPConfigs = () => {
    const values: Record<string, string | boolean> = transformData(getValues());
    const promiseArray = Object.keys(values).reduce((promiseArr, key) => {
      if (values[key] !== (initialData as any)[key]) {
        const keyName = `${LDAPPath}.${key}`;
        const value =
          isString(values[key]) &&
          !['ldap_default_role', 'ldap_group_search_scope', 'ldap_tls_protocol'].includes(key)
            ? `"${values[key]}"`
            : values[key];

        promiseArr.push(
          values[key] !== ''
            ? setRunTimeConfig({
                key: keyName,
                value
              })
            : deleteRunTimeConfig({
                key: keyName
              })
        );
      }

      return promiseArr;
    }, [] as Promise<AxiosResponse>[]);
    return promiseArray;
  };

  if (isLoading) return <YBLoadingCircleIcon />;
  if (isError) return <YBErrorIndicator />;

  const UserDefaultRole = [
    {
      label: t('roles.readOnly'),
      value: UserDefaultRoleOptions.ReadOnly
    },
    {
      label: t('roles.connectOnly'),
      value: UserDefaultRoleOptions.ConnectOnly
    }
  ];

  const ldapEnabled = watch('use_ldap');
  const securityProtocol = watch('ldap_security');
  const useSearchAndBind = watch('use_search_and_bind');
  const ldapUseQuery = watch('ldap_group_use_query');

  const toolTip = (content: string) => {
    return (
      <YBTooltip title={content} placement="top">
        <i className={`fa fa-info-circle ${classes.infoIcon}`} />
      </YBTooltip>
    );
  };

  return (
    <RbacValidator
      customValidateFunction={(userPerm) =>
        find(userPerm, { actions: [Action.SUPER_ADMIN_ACTIONS] }) !== undefined
      }
    >
      <div className={classes.root}>
        <div className={classes.header}>
          <Typography variant="h5">{t('title')}</Typography>
          <span className={classes.ldapEnabled}>
            <YBToggleField
              control={control}
              name="use_ldap"
              label={ldapEnabled ? t('ldapEnabled') : t('ldapDisabled')}
              onChange={(e) => {
                if (!e.target.checked) {
                  toggleldapDisableModal(true);
                } else {
                  setRunTimeConfig({
                    key: `${LDAPPath}.use_ldap`,
                    value: String(e.target.checked)
                  }).then(() => {
                    toast.success(t('messages.ldapEnabled'), TOAST_OPTIONS);
                    setValue('use_ldap', true);
                  });
                }
              }}
              data-testid="ldap-toggle"
            />
            {toolTip(t('infos.ldapEnabled'))}
          </span>
        </div>
        <div className={classes.configurations}>
          <YBInputField
            control={control}
            name="ldap_url"
            label={
              <>
                {t('ldapURL')}
                {toolTip(t('infos.ldapURL'))}
              </>
            }
            fullWidth
            disabled={!ldapEnabled}
            data-testid="ldap-url"
          />
          <YBRadioGroupField
            label={
              <>
                {t('ldapSecurity')}
                {toolTip(t('infos.ldapSecurity'))}
              </>
            }
            name="ldap_security"
            options={LDAPSecurityOptions}
            control={control}
            orientation="horizontal"
            isDisabled={!ldapEnabled}
            data-testid="ldap-security"
          />
          {(securityProtocol === SecurityOption.ENABLE_LDAPS ||
            securityProtocol === SecurityOption.ENABLE_LDAP_START_TLS) && (
            <YBRadioGroupField
              label={
                <>
                  {t('tlsProtocol')}
                  {toolTip(t('infos.tlsProtocol'))}
                </>
              }
              name="ldap_tls_protocol"
              options={TLSVersions}
              control={control}
              orientation="horizontal"
              isDisabled={!ldapEnabled}
              data-testid="tls-protocol"
            />
          )}
          <YBInputField
            control={control}
            name="ldap_basedn"
            label={
              <>
                {t('ldapBaseDN')}
                {toolTip(t('infos.ldapBaseDN'))}
              </>
            }
            fullWidth
            disabled={!ldapEnabled}
            data-testid="ldap-basedn"
          />
          <YBInputField
            control={control}
            name="ldap_dn_prefix"
            label={
              <>
                {t('ldapDNPrefix')}
                {toolTip(t('infos.ldapDNPrefix'))}
              </>
            }
            fullWidth
            disabled={!ldapEnabled}
            data-testid="ldap-dn-prefix"
          />
          <YBInputField
            control={control}
            name="ldap_customeruuid"
            label={
              <>
                {t('customerUUID')}
                {toolTip(t('infos.customerUUID'))}
              </>
            }
            fullWidth
            disabled={!ldapEnabled}
            data-testid="ldap-customeruuid"
          />
          <YBRadioGroupField
            label={
              <>
                {t('searchAndBind')}
                {toolTip(t('infos.searchAndBind'))}
              </>
            }
            name="use_search_and_bind"
            options={AuthModes}
            control={control}
            orientation="horizontal"
            isDisabled={!ldapEnabled}
            data-testid="search-and-bind"
          />
          {String(useSearchAndBind) === 'true' && (
            <>
              <YBInputField
                control={control}
                name="ldap_search_attribute"
                label={
                  <>
                    {t('searchAttribute')}
                    {toolTip(t('infos.searchAttribute'))}
                  </>
                }
                fullWidth
                disabled={!ldapEnabled}
                data-testid="ldap-search-attribute"
              />
              <YBInputField
                control={control}
                name="ldap_search_filter"
                label={
                  <>
                    {t('searchFilter')}
                    {toolTip(t('infos.searchFilter'))}
                  </>
                }
                fullWidth
                disabled={!ldapEnabled}
                data-testid="ldap-search-filter"
              />
            </>
          )}
          <Typography variant="body1" className={classes.serviceAccountHeader}>
            {t('addServiceAccountDetails')}
          </Typography>
          <YBInputField
            control={control}
            name="ldap_service_account_distinguished_name"
            label={
              <>
                {t('serviceAccountName')}
                {toolTip(t('infos.serviceAccountName'))}
              </>
            }
            fullWidth
            disabled={!ldapEnabled}
            helperText={t('infos.serviceAccountNameHelpText')}
            data-testid="ldap-service-account-distinguished-name"
          />
          <YBInputField
            control={control}
            name="ldap_service_account_password"
            label={
              <>
                {t('serviceAccountPassword')}
                {toolTip(t('infos.serviceAccountPassword'))}
              </>
            }
            fullWidth
            disabled={!ldapEnabled}
            data-testid="ldap-service-account-password"
          />
        </div>
        <Typography variant="h5" className={classes.roleHeader}>
          {t('roles.title')}
        </Typography>
        <div className={classes.roleAndGroupConfigurations}>
          <div className={classes.roleConfigurations}>
            <div className={classes.roleSettingsHeader}>
              <User />
              <Typography variant="body1">{t('roles.userRoleSettings')}</Typography>
            </div>
            <div className={classes.roleSettings}>
              <Typography variant="body2">{t('roles.userDefaultRole')}</Typography>
              <div className={classes.roleField}>
                <YBRadioGroupField
                  name="ldap_default_role"
                  options={UserDefaultRole}
                  control={control}
                  orientation="horizontal"
                  isDisabled={!ldapEnabled}
                  data-testid="ldap-default-role"
                />
                {toolTip(t('infos.connectOnly'))}
              </div>
            </div>
          </div>
          <Accordion
            expanded={groupSettingsExpanded}
            onChange={() => setGroupSettingsExpanded(!groupSettingsExpanded)}
            className={classes.groupSettings}
            data-testid="group-settings-tab"
          >
            <AccordionSummary expandIcon={<ArrowDropDown className={classes.groupsIcon} />}>
              <div className={classes.groupHeader}>
                <UserGroupsIcon className={classes.groupsIcon} />
                <Typography variant="body1">{t('group.title')}</Typography>
              </div>
              {t('group.helpText')}
            </AccordionSummary>
            <AccordionDetails className={classes.accordionDetails}>
              <YBRadioGroupField
                label={t('useQuery')}
                name="ldap_group_use_query"
                options={LDAPUseQueryOptions}
                control={control}
                orientation="horizontal"
                isDisabled={!ldapEnabled}
                data-testid="ldap-group-use-query"
              />
              {String(ldapUseQuery) === LDAPUseQuery.USER_ATTRIBUTE ? (
                <YBInputField
                  control={control}
                  name="ldap_group_member_of_attribute"
                  label={t('groupMemeberAttribute')}
                  fullWidth
                  disabled={!ldapEnabled}
                  data-testid="ldap-group-member-of-attribute"
                />
              ) : (
                <>
                  <YBInputField
                    control={control}
                    name="ldap_group_search_filter"
                    label={t('groupSearchFilter')}
                    fullWidth
                    disabled={!ldapEnabled}
                    helperText={
                      <Trans
                        t={t}
                        i18nKey={'infos.groupSearchFilter'}
                        components={{ b: <b />, br: <br /> }}
                      />
                    }
                    data-testid="ldap-group-search-filter"
                  />
                  <YBInputField
                    control={control}
                    name="ldap_group_search_base_dn"
                    label={
                      <>
                        {t('groupSearchBaseDN')}
                        {toolTip(t('infos.groupSearchBaseDN'))}
                      </>
                    }
                    fullWidth
                    disabled={!ldapEnabled}
                    data-testid="ldap-group-search-base-dn"
                  />
                  <YBSelectField
                    control={control}
                    name="ldap_group_search_scope"
                    label={t('ldapGroupSearchScope')}
                    className={classes.scope}
                    disabled={!ldapEnabled}
                    data-testid="ldap-group-search-scope"
                  >
                    {LDAPScopes.map((scope) => (
                      <MenuItem key={scope.value} value={scope.value}>
                        {scope.label}
                      </MenuItem>
                    ))}
                  </YBSelectField>
                </>
              )}
              <YBAlert
                variant={AlertVariant.Info}
                open
                icon={<BulbIcon />}
                text={
                  <Trans
                    i18nKey="userAuth.OIDC.roles.alertText"
                    components={{
                      a: (
                        <a
                          className={classes.link}
                          href={
                            isRbacEnabled()
                              ? `/admin/rbac?tab=groups`
                              : '/admin/user-management/user-groups'
                          }
                          rel="noreferrer"
                          target="_blank"
                        ></a>
                      )
                    }}
                  />
                }
                className={classes.alert}
              />
            </AccordionDetails>
          </Accordion>
        </div>

        <div className={classes.actions}>
          <YBButton
            variant="primary"
            size="large"
            onClick={() => {
              queryClient.invalidateQueries(LDAP_RUNTIME_CONFIGS_QUERY_KEY);
            }}
            data-testid="ldap-cancel"
          >
            {t('clear', { keyPrefix: 'common' })}
          </YBButton>
          <YBButton
            disabled={!ldapEnabled}
            variant="primary"
            size="large"
            onClick={() => {
              handleSubmit(() => {
                const promises = saveLDAPConfigs();
                if (promises.length === 0) {
                  return;
                }
                Promise.all(promises).then(() => {
                  queryClient.invalidateQueries(LDAP_RUNTIME_CONFIGS_QUERY_KEY);
                  toast.success(t('messages.ldapSaveSuccess'), TOAST_OPTIONS);
                });
              })();
            }}
            data-testid="ldap-save"
          >
            {t('save', { keyPrefix: 'common' })}
          </YBButton>
        </div>
      </div>

      <DisableAuthProviderModal
        onCancel={() => toggleldapDisableModal(false)}
        onSubmit={() => {
          setRunTimeConfig({
            key: `${LDAPPath}.use_ldap`,
            value: String(false)
          }).then(() => {
            setValue('use_ldap', false);
            toast.warning(t('messages.ldapDisabled'), TOAST_OPTIONS);
            toggleldapDisableModal(false);
          });
        }}
        visible={showldapDisableModal}
        type="LDAP"
      />
    </RbacValidator>
  );
};

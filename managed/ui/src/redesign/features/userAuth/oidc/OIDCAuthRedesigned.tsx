/*
 * Created on Tue Jul 09 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useState } from 'react';
import { useForm } from 'react-hook-form';
import { AxiosResponse } from 'axios';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { find, isString, keys } from 'lodash';
import { useToggle } from 'react-use';
import { Trans, useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { yupResolver } from '@hookform/resolvers/yup';
import { makeStyles, Typography } from '@material-ui/core';
import {
  AlertVariant,
  YBAlert,
  YBButton,
  YBInputField,
  YBRadioGroupField,
  YBToggleField,
  YBTooltip
} from '../../../components';
import OIDCMetadataModal from '../../../../components/users/UserAuth/OIDCMetadataModal';
import { DisableAuthProviderModal } from '../DisableAuthProvider';
import { YBErrorIndicator, YBLoadingCircleIcon } from '../../../../components/common/indicators';
import { setShowJWTTokenInfo, setSSO } from '../../../../config';
import { isRbacEnabled } from '../../rbac/common/RbacUtils';
import { RbacValidator } from '../../rbac/common/RbacApiPermValidator';
import { escapeStr, TOAST_OPTIONS, UserDefaultRoleOptions } from '../UserAuthUtils';
import { RunTimeConfigEntry } from '../../universe/universe-form/utils/dto';
import { api } from '../../universe/universe-form/utils/api';
import { Action } from '../../rbac';
import { getOIDCValidationSchema } from './OIDCValidationSchema';
import { OIDC_PATH, OIDC_RUNTIME_CONFIGS_QUERY_KEY } from '../../rbac/groups/components/GroupUtils';
import { OIDC_FIELDS, OIDCFormProps } from './OIDCConstants';
import { ReactComponent as User } from '../../../../redesign/assets/user-outline.svg';
import { ReactComponent as BulbIcon } from '../../../../redesign/assets/bulb.svg';

const useStyles = makeStyles((theme) => ({
  root: {
    width: '680px',
    padding: '24px',
    height: '1200px'
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
    flexDirection: 'column',
    '& .MuiFormLabel-root': {
      textTransform: 'capitalize',
      color: '#333',
      fontSize: '13px',
      fontWeight: 400,
      marginBottom: '6px'
    }
  },
  roleHeader: {
    marginTop: '56px',
    marginBottom: '16px'
  },
  roleConfigurations: {
    padding: '24px',
    border: `1px solid ${theme.palette.ybacolors.backgroundGrayDark}`,
    borderRadius: '8px'
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
    background: theme.palette.primary[200]
  },
  link: {
    color: 'inherit',
    textDecoration: 'underline'
  },
  oidcProviderConfig: {
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
  oidcEnabled: {
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
  }
}));

const transformProviderMetaData = (oidcProviderMetadata: string | undefined) => {
  if (!oidcProviderMetadata) {
    return null;
  }
  const escStr = oidcProviderMetadata ? oidcProviderMetadata.replace(/[\r\n]/gm, '') : null;
  const str = escStr && JSON.stringify(JSON.parse(escStr));

  return str ? '""' + str + '""' : '';
};

const initializeFormValues = (configEntries: RunTimeConfigEntry[]) => {
  const oidcFields = OIDC_FIELDS.map((ef) => `${OIDC_PATH}.${ef}`);
  const oidcConfigs = configEntries.filter((config) => oidcFields.includes(config.key));
  const formData: Partial<OIDCFormProps> = oidcConfigs.reduce((fData, config) => {
    const [, key] = config.key.split(`${OIDC_PATH}.`);
    if (key === 'oidcProviderMetadata') {
      const escapedStr = config.value ? escapeStr(config.value).replace(/\\/g, '') : '';
      fData[key] = escapedStr ? JSON.stringify(JSON.parse(escapedStr), null, 2) : '';
    } else {
      fData[key] = escapeStr(config.value);
    }
    if (key === 'showJWTInfoOnLogin' || key === 'use_oauth') {
      fData[key] = config.value === 'true';
    }
    return fData;
  }, {} as any);

  const finalFormData = {
    ...formData
  };

  return finalFormData;
};

export const OIDCAuthNew = () => {
  const classes = useStyles();

  const { t } = useTranslation('translation', {
    keyPrefix: 'userAuth.OIDC'
  });

  const { control, setValue, watch, getValues, handleSubmit } = useForm<OIDCFormProps>({
    defaultValues: {
      oidc_default_role: UserDefaultRoleOptions.ReadOnly
    },
    resolver: yupResolver(getOIDCValidationSchema(t))
  });

  const { mutateAsync: setRunTimeConfig } = useMutation(
    ({ key, value }: { key: string; value: unknown }) => {
      return api.setRunTimeConfig({ key, value });
    },
    {
      onError(_error, variables) {
        toast.error(t('messages.oidcSaveFailed', { key: variables }), TOAST_OPTIONS);
      }
    }
  );

  const { mutateAsync: deleteRunTimeConfig } = useMutation(
    ({ key }: { key: string }) => {
      return api.deleteRunTimeConfig({ key });
    },
    {
      onError(_error, variables) {
        toast.error(t('messages.oidcSaveFailed', { key: variables }), TOAST_OPTIONS);
      }
    }
  );

  const [showMetadataModel, toggleMetadataModal] = useToggle(false);
  const [showOIDCDisableModal, toggleOIDCDisableModal] = useToggle(false);
  const [initialData, setInitialData] = useState<Partial<OIDCFormProps>>({});

  const queryClient = useQueryClient();

  const { isLoading, isError } = useQuery(
    [OIDC_RUNTIME_CONFIGS_QUERY_KEY],
    () => api.fetchRunTimeConfigs(true),
    {
      onSuccess(data) {
        const formData = initializeFormValues(data.configEntries);
        setInitialData(formData);
        Object.entries(formData).forEach(([key, value]) => {
          setValue((key as unknown) as keyof OIDCFormProps, value, { shouldValidate: false });
        });
      }
    }
  );

  const saveOIDCConfigs = () => {
    const promiseArr: Promise<AxiosResponse>[] = [];

    keys(initialData).map((key) => {
      if ((initialData as any)[key] === getValues(key as any)) {
        return;
      }
      const value = getValues(key as keyof OIDCFormProps);
      if (value === undefined) {
        promiseArr.push(deleteRunTimeConfig({ key: `${OIDC_PATH}.${key}` }));
      } else {
        let val: any = getValues(key as keyof OIDCFormProps);
        if (key === `oidcProviderMetadata`) {
          val = transformProviderMetaData(getValues(key as keyof OIDCFormProps) as string);
        }
        if (key === `showJWTInfoOnLogin`) {
          val = String(val);
          setShowJWTTokenInfo(val);
          setSSO(val);
        }
        promiseArr.push(
          setRunTimeConfig({
            key: `${OIDC_PATH}.${key}`,
            value: key !== 'oidc_default_role' && isString(val) ? `"${val}"` : val
          })
        );
      }
    });
    return promiseArr;
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

  const oauthEnabled = watch('use_oauth');

  const { oidcProviderMetadata } = getValues();

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
          <span className={classes.oidcEnabled}>
            <YBToggleField
              control={control}
              name="use_oauth"
              label={oauthEnabled ? t('oidcEnabled') : t('oidcDisabled')}
              onChange={(e) => {
                if (!e.target.checked) {
                  toggleOIDCDisableModal(true);
                } else {
                  setRunTimeConfig({
                    key: `${OIDC_PATH}.use_oauth`,
                    value: String(e.target.checked)
                  }).then(() => {
                    toast.success(t('messages.oidcEnabled'), TOAST_OPTIONS);
                    setValue('use_oauth', true);
                  });
                }
              }}
              data-testid="oidc-toggle"
            />
            {toolTip(t('infos.oidcEnabled'))}
          </span>
        </div>
        <div className={classes.configurations}>
          <YBInputField
            control={control}
            name="clientID"
            label={
              <>
                {t('clientID')}
                {toolTip(t('infos.clientID'))}
              </>
            }
            fullWidth
            disabled={!oauthEnabled}
            required
            data-testid="clientID"
          />
          <YBInputField
            control={control}
            name="secret"
            label={
              <>
                {t('clientSecret')}
                {toolTip(t('infos.clientSecret'))}
              </>
            }
            fullWidth
            disabled={!oauthEnabled}
            required
            data-testid="clientSecret"
          />
          <YBInputField
            control={control}
            name="discoveryURI"
            label={
              <>
                {t('discoveryURL')}
                {toolTip(t('infos.discoveryURL'))}
              </>
            }
            fullWidth
            disabled={!oauthEnabled}
            required
            data-testid="discoveryURI"
          />
          <YBInputField
            control={control}
            name="oidcScope"
            label={
              <>
                {t('scope')}
                {toolTip(t('infos.scope'))}
              </>
            }
            fullWidth
            disabled={!oauthEnabled}
            data-testid="oidcScope"
          />
          <YBInputField
            control={control}
            name="oidcEmailAttribute"
            label={
              <>
                {t('emailAttribute')}
                {toolTip(t('infos.emailAttribute'))}
              </>
            }
            fullWidth
            disabled={!oauthEnabled}
            data-testid="oidcEmailAttribute"
          />
          <YBInputField
            control={control}
            name="oidcRefreshTokenEndpoint"
            label={
              <>
                {t('refreshTokenURL')}
                {toolTip(t('infos.refreshTokenURL'))}
              </>
            }
            fullWidth
            disabled={!oauthEnabled}
            data-testid="oidcRefreshTokenEndpoint"
          />
          <YBToggleField
            control={control}
            name="showJWTInfoOnLogin"
            label={
              <>
                {t('displayJWTToken')}
                {toolTip(t('infos.jwtTokenURL'))}
              </>
            }
            disabled={!oauthEnabled}
            data-testid="showJWTInfoOnLogin"
          />
          <YBButton
            variant="secondary"
            className={classes.oidcProviderConfig}
            disabled={!oauthEnabled}
            onClick={() => {
              toggleMetadataModal(true);
            }}
            data-testid="oidcProviderConfig"
          >
            {t('oidcProviderConfig')}
          </YBButton>
        </div>
        <Typography variant="h5" className={classes.roleHeader}>
          {t('roles.title')}
        </Typography>
        <div className={classes.roleConfigurations}>
          <div className={classes.roleSettingsHeader}>
            <User />
            <Typography variant="body1">{t('roles.userRoleSettings')}</Typography>
          </div>
          <div className={classes.roleSettings}>
            <Typography variant="body2">{t('roles.userDefaultRole')}</Typography>
            <div className={classes.roleField}>
              <YBRadioGroupField
                name="oidc_default_role"
                options={UserDefaultRole}
                control={control}
                orientation="horizontal"
                isDisabled={!oauthEnabled}
                data-testid="oidc_default_role"
              />
              {toolTip(t('infos.connectOnly'))}
            </div>
          </div>
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
        </div>
        <div className={classes.actions}>
          <YBButton
            variant="primary"
            size="large"
            onClick={() => {
              queryClient.invalidateQueries(OIDC_RUNTIME_CONFIGS_QUERY_KEY);
            }}
            data-testid="cancel"
          >
            {t('clear', { keyPrefix: 'common' })}
          </YBButton>
          <YBButton
            disabled={!oauthEnabled}
            variant="primary"
            size="large"
            onClick={() => {
              handleSubmit(() => {
                const promises = saveOIDCConfigs();
                if (promises.length === 0) {
                  return;
                }
                Promise.all(promises).then(() => {
                  queryClient.invalidateQueries(OIDC_RUNTIME_CONFIGS_QUERY_KEY);
                  toast.success(t('messages.oidcSaveSuccess'), TOAST_OPTIONS);
                });
              })();
            }}
            data-testid="save"
          >
            {t('save', { keyPrefix: 'common' })}
          </YBButton>
        </div>
        <OIDCMetadataModal
          open={showMetadataModel}
          value={oidcProviderMetadata}
          onClose={() => {
            toggleMetadataModal(false);
          }}
          onSubmit={(value: string) => {
            setValue('oidcProviderMetadata', value);
            toggleMetadataModal(false);
          }}
        ></OIDCMetadataModal>
        <DisableAuthProviderModal
          onCancel={() => toggleOIDCDisableModal(false)}
          onSubmit={() => {
            setRunTimeConfig({
              key: `${OIDC_PATH}.use_oauth`,
              value: String(false)
            }).then(() => {
              setValue('use_oauth', false);
              toast.warning(t('messages.oidcDisabled'), TOAST_OPTIONS);
              toggleOIDCDisableModal(false);
            });
          }}
          visible={showOIDCDisableModal}
          type="OIDC"
        />
      </div>
    </RbacValidator>
  );
};

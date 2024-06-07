import { FC, useEffect, useState } from 'react';
import { MenuItem, Dropdown } from 'react-bootstrap';
import { useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';
import { fetchProviderList } from '../../../api/admin';
import { RunTimeConfigScope, RuntimeConfigScopeProps } from '../../../redesign/utils/dtos';
import { ConfigData } from '../ConfigData';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';

import '../AdvancedConfig.scss';

export const ProviderRuntimeConfig: FC<RuntimeConfigScopeProps> = ({
  configTagFilter,
  fetchRuntimeConfigs,
  setRuntimeConfig,
  deleteRunTimeConfig,
  resetRuntimeConfigs
}) => {
  const { t } = useTranslation();
  const providers = useQuery(['providers'], () => fetchProviderList().then((res: any) => res.data));
  const [providerDropdownValue, setProviderDropdownValue] = useState<string>();
  const [providerUUID, setProviderUUID] = useState<string>();

  const onProviderDropdownChanged = (providerName: string, providerUUID: string) => {
    setProviderDropdownValue(providerName);
    setProviderUUID(providerUUID);
  };

  useEffect(() => {
    resetRuntimeConfigs();
    if (providerUUID) {
      fetchRuntimeConfigs(providerUUID);
    }
  }, [providerUUID]); // eslint-disable-line react-hooks/exhaustive-deps

  if (providers.isError) {
    return (
      <YBErrorIndicator customErrorMessage={t('admin.advanced.globalConfig.GenericConfigError')} />
    );
  }
  if (providers.isLoading || (providers.isIdle && providers.data === undefined)) {
    return <YBLoading />;
  }

  const providersList = providers.data;
  if (providersList.length <= 0) {
    return (
      <YBErrorIndicator customErrorMessage={t('admin.advanced.globalConfig.ProviderConfigError')} />
    );
  }
  if (providersList.length > 0 && providerDropdownValue === undefined) {
    setProviderDropdownValue(providersList[0].name);
  }
  if (providersList.length > 0 && providerUUID === undefined) {
    setProviderUUID(providersList[0].uuid);
  }

  return (
    <div className="provider-runtime-config-container">
      <div className="provider-runtime-config-container__display">
        <span className="provider-runtime-config-container__label">
          {t('admin.advanced.globalConfig.SelectProvider')}
        </span>
        &nbsp;&nbsp;
        <Dropdown id="providerRuntimeConfigDropdown" className="provider-runtime-config-dropdown">
          <Dropdown.Toggle>
            <span className="provider-config-dropdown-value">{providerDropdownValue}</span>
          </Dropdown.Toggle>
          <Dropdown.Menu>
            {providersList?.length > 0 &&
              providersList.map((provider: any, providerIdx: number) => {
                return (
                  <MenuItem
                    eventKey={`provider-${providerIdx}`}
                    key={`${provider.uuid}`}
                    active={providerDropdownValue === provider.name}
                    onSelect={() => onProviderDropdownChanged?.(provider.name, provider.uuid)}
                  >
                    {provider.name}
                  </MenuItem>
                );
              })}
          </Dropdown.Menu>
        </Dropdown>
      </div>
      <ConfigData
        setRuntimeConfig={setRuntimeConfig}
        deleteRunTimeConfig={deleteRunTimeConfig}
        scope={RunTimeConfigScope.PROVIDER}
        providerUUID={providerUUID}
        configTagFilter={configTagFilter}
      />
    </div>
  );
};

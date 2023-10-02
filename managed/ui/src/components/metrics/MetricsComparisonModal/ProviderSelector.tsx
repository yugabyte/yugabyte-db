import React, { FC } from 'react';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { MenuItem, Dropdown } from 'react-bootstrap';
import { fetchProviderList } from '../../../api/admin';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { MetricConsts } from '../constants';
import { CloudType } from '../../../redesign/helpers/dtos';

interface ProviderSelectorProps {
  onProviderChanged: (universe: any) => void;
  currentSelectedProvider: string;
  currentSelectedProviderUUID: string;
}

export const ProviderSelector: FC<ProviderSelectorProps> = ({
  onProviderChanged,
  currentSelectedProvider,
  currentSelectedProviderUUID
}) => {
  const { t } = useTranslation();
  let providerItems: any = [];

  const providers = useQuery(['providers'], () => fetchProviderList().then((res: any) => res.data));

  if (providers.isError) {
    return <YBErrorIndicator customErrorMessage={t('providerSelector.errorMessage')} />;
  }

  if (providers.isLoading) {
    return <YBLoading />;
  }

  const providerList = providers.data.filter((provider: any) => provider.code === CloudType.onprem);
  // eslint-disable-next-line react/display-name
  providerItems = providerList?.map((provider: any, providerIdx: number) => {
    return (
      <MenuItem
        key={`provider-${providerIdx}`}
        onSelect={() => onProviderChanged(provider)}
        eventKey={`provider-${providerIdx}`}
        active={
          provider.name === currentSelectedProvider && provider.uuid === currentSelectedProviderUUID
        }
      >
        <span className="provider-name">{provider.name}</span>
      </MenuItem>
    );
  });

  // By default we need to have 'All regions' populated
  const defaultMenuItem = (
    <MenuItem
      onSelect={() => onProviderChanged(MetricConsts.ALL)}
      key={MetricConsts.ALL}
      active={currentSelectedProvider === MetricConsts.ALL}
      eventKey={MetricConsts.ALL}
    >
      {t('providerSelector.all')}
    </MenuItem>
  );
  providerItems.splice(0, 0, defaultMenuItem);

  return (
    <div className="provider-picker-container pull-left">
      <Dropdown id="providerFilterDropdown" className="provider-filter-dropdown">
        <Dropdown.Toggle className="dropdown-toggle-button">
          <span className="default-value">
            {currentSelectedProvider === MetricConsts.ALL
              ? providerItems[0]
              : currentSelectedProvider}
          </span>
        </Dropdown.Toggle>
        <Dropdown.Menu>{providerItems.length > 1 && providerItems}</Dropdown.Menu>
      </Dropdown>
    </div>
  );
};

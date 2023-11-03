import { FC, useEffect, useState } from 'react';
import { MenuItem, Dropdown } from 'react-bootstrap';
import { useSelector } from 'react-redux';
import { useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';
import { fetchCustomersList } from '../../../api/admin';
import { RunTimeConfigScope, RuntimeConfigScopeProps } from '../../../redesign/utils/dtos';
import { ConfigData } from '../ConfigData';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';

import '../AdvancedConfig.scss';

export const CustomerRuntimeConfig: FC<RuntimeConfigScopeProps> = ({
  configTagFilter,
  fetchRuntimeConfigs,
  setRuntimeConfig,
  deleteRunTimeConfig,
  resetRuntimeConfigs
}) => {
  const { t } = useTranslation();
  const customers = useQuery(['customers'], () =>
    fetchCustomersList().then((res: any) => res.data)
  );
  const currentUserInfo = useSelector((state: any) => state.customer.currentUser.data);
  const currentCustomerInfo = useSelector((state: any) => state.customer.currentCustomer.data);
  const isSuperAdmin = ['SuperAdmin'].includes(currentUserInfo.role);
  const [customerDropdownValue, setcustomerDropdownValue] = useState<string>();
  const [customerUUID, setCustomerUUID] = useState<string>();

  const onCustomerDropdownChanged = (customerName: string, customerUUID: string) => {
    setcustomerDropdownValue(customerName);
    setCustomerUUID(customerUUID);
  };

  useEffect(() => {
    resetRuntimeConfigs();
    if (customerUUID) {
      // Super admin should be able to fetch runtime configs of all customers in case of multi-tenancy
      fetchRuntimeConfigs(customerUUID);
    }
  }, [customerUUID]); // eslint-disable-line react-hooks/exhaustive-deps

  if (customers.isError) {
    return (
      <YBErrorIndicator customErrorMessage={t('admin.advanced.globalConfig.GenericConfigError')} />
    );
  }
  if (customers.isLoading || (customers.isIdle && customers.data === undefined)) {
    return <YBLoading />;
  }

  let customersList = customers.data;
  if (customersList.length <= 0) {
    return (
      <YBErrorIndicator customErrorMessage={t('admin.advanced.globalConfig.CustomerConfigError')} />
    );
  } else if (customersList.length > 0) {
    if (!isSuperAdmin) {
      customersList = customersList.filter(
        (customer: any) => customer?.uuid === currentCustomerInfo?.uuid
      );
    }
    if (customerDropdownValue === undefined) {
      setcustomerDropdownValue(customersList[0].name);
    }
    if (customerUUID === undefined) {
      setCustomerUUID(customersList[0].uuid);
    }
  }

  return (
    <div className="customer-runtime-config-container">
      <div className="customer-runtime-config-container__display">
        <span className="customer-runtime-config-container__label">
          {t('admin.advanced.globalConfig.SelectCustomer')}
        </span>
        &nbsp;&nbsp;
        <Dropdown id="customerRuntimeConfigDropdown" className="customer-runtime-config-dropdown">
          <Dropdown.Toggle>
            <span className="customer-config-dropdown-value">{customerDropdownValue}</span>
          </Dropdown.Toggle>
          <Dropdown.Menu>
            {customersList?.length > 0 &&
              customersList.map((customer: any, customerIdx: number) => {
                return (
                  <MenuItem
                    eventKey={`provider-${customerIdx}`}
                    key={`${customer.uuid}`}
                    active={customerDropdownValue === customer.name}
                    onSelect={() => onCustomerDropdownChanged?.(customer.name, customer.uuid)}
                  >
                    {customer.name}
                  </MenuItem>
                );
              })}
          </Dropdown.Menu>
        </Dropdown>
      </div>
      <ConfigData
        setRuntimeConfig={setRuntimeConfig}
        deleteRunTimeConfig={deleteRunTimeConfig}
        scope={RunTimeConfigScope.CUSTOMER}
        customerUUID={customerUUID}
        configTagFilter={configTagFilter}
      />
    </div>
  );
};

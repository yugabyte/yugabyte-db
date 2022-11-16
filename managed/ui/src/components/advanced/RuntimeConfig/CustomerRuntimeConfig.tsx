import React, { FC, useEffect, useState } from 'react';
import { MenuItem, Dropdown } from 'react-bootstrap';
import { useQuery } from 'react-query';

import { fetchCustomersList } from '../../../api/admin';
import { RunTimeConfigScope } from '../../../redesign/helpers/dtos';
import { ConfigData } from '../ConfigData';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';

import '../AdvancedConfig.scss';

interface CustomerRuntimeConfigProps {
  fetchRuntimeConfigs: (scope?: string) => void;
  setRuntimeConfig: (key: string, value: string) => void;
  deleteRunTimeConfig: (key: string) => void;
  resetRuntimeConfigs: () => void;
}

export const CustomerRuntimeConfig: FC<CustomerRuntimeConfigProps> = ({
  fetchRuntimeConfigs,
  setRuntimeConfig,
  deleteRunTimeConfig,
  resetRuntimeConfigs,
}) => {
  const customers = useQuery(['customers'], () =>
    fetchCustomersList().then((res) => res.data)
  );
  const [customerDropdownValue, setcustomerDropdownValue] = useState<string>();
  const [customerUUID, setCustomerUUID] = useState<string>();

  const onCustomerDropdownChanged = (customerName: string, customerUUID: string) => {
    setcustomerDropdownValue(customerName);
    setCustomerUUID(customerUUID);
  }

  useEffect(() => {
    resetRuntimeConfigs();
    if (customerUUID) {
      fetchRuntimeConfigs(customerUUID);
    }
  }, [customerUUID]); // eslint-disable-line react-hooks/exhaustive-deps

  if (customers.isError) {
    return (
      <YBErrorIndicator customErrorMessage="Please try again" />
    );
  }
  if (customers.isLoading || (customers.isIdle && customers.data === undefined)) {
    return <YBLoading />
  }

  const customersList = customers.data;
  if (customersList.length > 0 && customerDropdownValue === undefined) {
    setcustomerDropdownValue(customersList[0].name);
  }
  if (customersList.length > 0 && customerUUID === undefined) {
    setCustomerUUID(customersList[0].uuid);
  }
  return (
    <div className="customer-runtime-config-container">
      <div className="customer-runtime-config-container__display">
        <span className="customer-runtime-config-container__label"> {"Select Customer:"}</span>
        &nbsp;&nbsp;
        <Dropdown
          id="customerRuntimeConfigDropdown"
          className="customer-runtime-config-dropdown"
        >
          <Dropdown.Toggle>
            <span className="customer-config-dropdown-value">{customerDropdownValue}</span>
          </Dropdown.Toggle>
          <Dropdown.Menu>
            {customersList?.length > 0 &&
              customersList.map((customer: any, customerIdx: number) => {
                return (<MenuItem
                  eventKey={`provider-${customerIdx}`}
                  key={`${customer.uuid}`}
                  active={customerDropdownValue === customer.name}
                  onSelect={() => onCustomerDropdownChanged?.(customer.name, customer.uuid)}
                >
                  {customer.name}
                </MenuItem>);
              })
            }
          </Dropdown.Menu>
        </Dropdown>
      </div>
      <ConfigData
        setRuntimeConfig={setRuntimeConfig}
        deleteRunTimeConfig={deleteRunTimeConfig}
        scope={RunTimeConfigScope.CUSTOMER}
        customerUUID={customerUUID}
      />
    </div>
  );
}

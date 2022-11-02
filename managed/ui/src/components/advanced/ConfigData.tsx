import React, { FC, useState } from 'react';
import { DropdownButton, MenuItem } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { useTranslation } from 'react-i18next';
import { useSelector } from 'react-redux';

import { YBCheckBox } from '../common/forms/fields';
import { YBErrorIndicator, YBLoading } from '../common/indicators';
import { EditConfig } from './EditConfig';
import { ResetConfig } from './ResetConfig';
import { getPromiseState } from '../../utils/PromiseUtils';
import { isNonEmptyArray } from '../../utils/ObjectUtils';

import './AdvancedConfig.scss';

interface GlobalConfigProps {
  setRuntimeConfig: (key: string, value: string, scope?: string) => void;
  deleteRunTimeConfig: (key: string, scope?: string) => void;
  scope: string;
  universeUUID?: string;
  providerUUID?: string;
  customerUUID?: string;
}

export const ConfigData: FC<GlobalConfigProps> = ({
  setRuntimeConfig,
  deleteRunTimeConfig,
  scope,
  universeUUID,
  providerUUID,
  customerUUID
}) => {
  const { t } = useTranslation();
  const runtimeConfigs = useSelector((state: any) => state.customer.runtimeConfigs);
  // Helps in deciding if the logged in user can mutate the config values
  const isScopeMutable = runtimeConfigs?.data?.mutableScope;
  const [editConfig, setEditConfig] = useState(false);
  const [resetConfig, setResetConfig] = useState(false);
  const [showOverridenValues, setShowOverridenValues] = useState(false);
  const [configData, setConfigData] = useState({
    configID: 0,
    configKey: '',
    configValue: '',
    isConfigInherited: true
  });

  if (runtimeConfigs?.data && getPromiseState(runtimeConfigs).isLoading()) {
    return <YBLoading />
  } else if (runtimeConfigs?.error) {
    return <YBErrorIndicator
      customErrorMessage={t('admin.advanced.globalConfig.GlobalConfigReqFailed')} />
  }

  const globalConfigEntries = runtimeConfigs?.data?.configEntries;
  let listItems: Array<any> = [];
  if (isNonEmptyArray(globalConfigEntries)) {
    listItems = globalConfigEntries.map(function (entry: any, idx: number) {
      return {
        configID: idx + 1,
        configKey: entry.key,
        configValue: entry.value,
        isConfigInherited: entry.inherited
      };
    });
  }

  const openEditConfig = (row: any) => {
    setEditConfig(true);
    setConfigData(row);
  };

  const openResetConfig = (row: any) => {
    setResetConfig(true);
    setConfigData(row);
  };

  const formatActionButtons = (cell: any, row: any) => {
    return (
      <DropdownButton
        className="btn btn-default"
        title="Actions"
        id="runtime-config-nested-dropdown"
        pullRight
      >
        <MenuItem
          onClick={() => {
            openEditConfig(row)
          }}
        >
          {t('admin.advanced.globalConfig.ModelEditConfigTitle')}
        </MenuItem>

        {(!row.isConfigInherited) && <MenuItem
          onClick={() => {
            openResetConfig(row)
          }}
        >
          {t('admin.advanced.globalConfig.ModelResetConfigTitle')}
        </MenuItem>}
      </DropdownButton>
    );
  };

  const rowClassNameFormat = (row: any) => {
    return row.isConfigInherited ? "config-inherited-row" : "config-non-inherited-row";
  }

  return (
    <div className="runtime-config-data-container">
      <span className="runtime-config-data-container__check">
        <YBCheckBox
          label={
            <span className="checkbox-label">
              {t('admin.advanced.globalConfig.ShowOverridenConfigs')}
            </span>}
          input={{
            onChange: (e: any) => {
              setShowOverridenValues(e.target.checked)
            }
          }}
        />
      </span>
      <BootstrapTable
        data={showOverridenValues ? listItems.filter((item) => !item.isConfigInherited) : listItems}
        pagination
        search
        multiColumnSearch
        trClassName={rowClassNameFormat}
      >
        <TableHeaderColumn dataField="configID" isKey={true} hidden={true} />
        <TableHeaderColumn
          dataField={'configKey'}
          width="15%"
          columnClassName={'table-name-label yb-table-cell'}
          dataSort
        >
          Config Key
        </TableHeaderColumn>
        <TableHeaderColumn
          dataField={'configValue'}
          width="15%"
        >
          Config Value
        </TableHeaderColumn>
        {isScopeMutable && (
          <TableHeaderColumn
            dataField={'actions'}
            columnClassName={'yb-actions-cell'}
            width="10%"
            dataFormat={formatActionButtons}
          >
            Actions
          </TableHeaderColumn>
        )}
      </BootstrapTable>
      {editConfig &&
        <EditConfig
          configData={configData}
          onHide={() => setEditConfig(false)}
          setRuntimeConfig={setRuntimeConfig}
          scope={scope}
          universeUUID={universeUUID}
          providerUUID={providerUUID}
          customerUUID={customerUUID}
        />}
      {resetConfig &&
        <ResetConfig
          configData={configData}
          onHide={() => setResetConfig(false)}
          deleteRunTimeConfig={deleteRunTimeConfig}
          scope={scope}
          universeUUID={universeUUID}
          providerUUID={providerUUID}
          customerUUID={customerUUID}
        />}
    </div>
  );
}

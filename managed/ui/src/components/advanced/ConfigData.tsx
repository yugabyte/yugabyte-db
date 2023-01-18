import React, { FC, useEffect, useState } from 'react';
import { DropdownButton, OverlayTrigger, MenuItem, Tooltip } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { useTranslation } from 'react-i18next';
import { useSelector } from 'react-redux';
import { YBCheckBox } from '../common/forms/fields';
import { YBErrorIndicator, YBLoading } from '../common/indicators';
import { EditConfig } from './EditConfig';
import { ResetConfig } from './ResetConfig';
import { RunTimeConfigData } from '../../redesign/helpers/dtos';
import { getPromiseState } from '../../utils/PromiseUtils';
import { isNonEmptyArray } from '../../utils/ObjectUtils';

import './AdvancedConfig.scss';

const ConfigScopePriority = {
  GLOBAL: 1,
  CUSTOMER: 2,
  UNIVERSE: 3,
  PROVIDER: 4
};

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
  const runtimeConfigsKeyInfo = useSelector((state: any) => state.customer.runtimeConfigsKeyInfo);
  // Helps in deciding if the logged in user can mutate the config values
  const isScopeMutable = runtimeConfigs?.data?.mutableScope;
  const [editConfig, setEditConfig] = useState<boolean>(false);
  const [resetConfig, setResetConfig] = useState<boolean>(false);
  const [showOverridenValues, setShowOverridenValues] = useState<boolean>(false);
  const [listItems, setListItems] = useState([{ isConfigInherited: false }]);
  const [configData, setConfigData] = useState<RunTimeConfigData>({
    configID: 0,
    configKey: '',
    configValue: '',
    isConfigInherited: true,
    displayName: '',
    helpTxt: '',
    type: '',
    scope: ''
  });

  const runtimeConfigEntries = runtimeConfigs?.data?.configEntries;

  useEffect(() => {
    if (isNonEmptyArray(runtimeConfigEntries)) {
      const runtimeConfigItems = runtimeConfigEntries
        .map((entry: any, idx: number) => {
          return {
            configID: idx + 1,
            configKey: entry.key,
            configValue: entry.value,
            isConfigInherited: entry.inherited
          };
        })
        ?.filter((item: any) => {
          return runtimeConfigsKeyInfo?.data?.find((keyInfo: any) => {
            let isScopeValid = false;
            if (ConfigScopePriority[scope] === 1) {
              isScopeValid = true;
            } else if (
              ConfigScopePriority[scope] === 2 &&
              ConfigScopePriority[keyInfo.scope] >= 2
            ) {
              isScopeValid = true;
            } else if (
              ConfigScopePriority[scope] === 3 &&
              ConfigScopePriority[keyInfo.scope] === 3
            ) {
              isScopeValid = true;
            } else if (
              ConfigScopePriority[scope] === 4 &&
              ConfigScopePriority[keyInfo.scope] === 4
            ) {
              isScopeValid = true;
            }
            if (keyInfo.key === item.configKey && isScopeValid) {
              item.displayName = keyInfo.displayName;
              item.helpTxt = keyInfo.helpTxt;
              item.type = keyInfo.dataType?.name;
              item.scope = keyInfo.scope;
            }
            return keyInfo.key === item.configKey && isScopeValid;
          });
        });
      setListItems(runtimeConfigItems);
    }
  }, [scope, universeUUID, providerUUID, customerUUID, runtimeConfigEntries]); // eslint-disable-line react-hooks/exhaustive-deps

  const openEditConfig = (row: any) => {
    setEditConfig(true);
    setConfigData(row);
  };

  const openResetConfig = (row: any) => {
    setResetConfig(true);
    setConfigData(row);
  };

  const formatDisplayName = (cell: any, row: any) => {
    return (
      <>
        {row.displayName}
        <OverlayTrigger
          placement="right"
          overlay={
            <Tooltip className="high-index" id="runtime-config-tooltip">
              {row.helpTxt}
            </Tooltip>
          }
        >
          <span>
            &nbsp;&nbsp;
            <i className="fa fa-question-circle yb-help-color yb-info-tip yb-table-cell-align" />
          </span>
        </OverlayTrigger>
      </>
    );
  };

  const formatActionButtons = (cell: any, row: any) => {
    return (
      <DropdownButton
        className="btn btn-default"
        title="Actions"
        id="runtime-config-nested-dropdown middle-aligned-table"
        pullRight
      >
        <MenuItem
          onClick={() => {
            openEditConfig(row);
          }}
        >
          {t('admin.advanced.globalConfig.ModelEditConfigTitle')}
        </MenuItem>

        {!row.isConfigInherited && (
          <MenuItem
            onClick={() => {
              openResetConfig(row);
            }}
          >
            {t('admin.advanced.globalConfig.ModelResetConfigTitle')}
          </MenuItem>
        )}
      </DropdownButton>
    );
  };

  if (runtimeConfigs?.data && getPromiseState(runtimeConfigs).isLoading()) {
    return <YBLoading />;
  } else if (runtimeConfigs?.error) {
    return (
      <YBErrorIndicator
        customErrorMessage={t('admin.advanced.globalConfig.GlobalConfigReqFailed')}
      />
    );
  }

  const rowClassNameFormat = (row: any) => {
    return row.isConfigInherited ? 'config-inherited-row' : 'config-non-inherited-row';
  };

  return (
    <div className="runtime-config-data-container">
      <span className="runtime-config-data-container__check">
        <YBCheckBox
          label={
            <span className="checkbox-label">
              {t('admin.advanced.globalConfig.ShowOverridenConfigs')}
            </span>
          }
          input={{
            onChange: (e: any) => {
              setShowOverridenValues(e.target.checked);
            },
            checked: showOverridenValues
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
          width="15%"
          className={'middle-aligned-table'}
          columnClassName={'yb-table-cell yb-table-cell-align'}
          dataFormat={formatDisplayName}
          dataSort
        >
          Display Name
        </TableHeaderColumn>
        <TableHeaderColumn
          dataField={'configKey'}
          width="15%"
          columnClassName={'yb-table-cell yb-table-cell-align'}
          dataSort
        >
          Config Key
        </TableHeaderColumn>
        <TableHeaderColumn
          dataField={'configValue'}
          width="15%"
          columnClassName={'yb-table-cell yb-table-cell-align'}
          dataSort
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
      {editConfig && (
        <EditConfig
          configData={configData}
          onHide={() => setEditConfig(false)}
          setRuntimeConfig={setRuntimeConfig}
          scope={scope}
          universeUUID={universeUUID}
          providerUUID={providerUUID}
          customerUUID={customerUUID}
        />
      )}
      {resetConfig && (
        <ResetConfig
          configData={configData}
          onHide={() => setResetConfig(false)}
          deleteRunTimeConfig={deleteRunTimeConfig}
          scope={scope}
          universeUUID={universeUUID}
          providerUUID={providerUUID}
          customerUUID={customerUUID}
        />
      )}
    </div>
  );
};

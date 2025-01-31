import { FC, useEffect, useState } from 'react';
import { find } from 'lodash';
import { DropdownButton, OverlayTrigger, MenuItem, Tooltip } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { useTranslation } from 'react-i18next';
import { useSelector } from 'react-redux';
import { YBCheckBox } from '../common/forms/fields';
import { YBErrorIndicator, YBLoading } from '../common/indicators';
import { EditConfig } from './EditConfig';
import { ResetConfig } from './ResetConfig';
import { RunTimeConfigData, RunTimeConfigScope } from '../../redesign/utils/dtos';
import { getPromiseState } from '../../utils/PromiseUtils';
import { isNonEmptyArray } from '../../utils/ObjectUtils';
import {
  RbacValidator,
  hasNecessaryPerm
} from '../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../redesign/features/rbac/ApiAndUserPermMapping';
import { Action } from '../../redesign/features/rbac';
import { DEFAULT_RUNTIME_GLOBAL_SCOPE } from '../../actions/customers';
import './AdvancedConfig.scss';

const DEFAULT_RUNTIME_TAG_FILTER = ['PUBLIC'];
const ConfigScopePriority = {
  GLOBAL: 1,
  CUSTOMER: 2,
  UNIVERSE: 3,
  PROVIDER: 4
};

interface ConfigDataProps {
  getRuntimeConfig: (key: string, scope?: string) => void;
  setRuntimeConfig: (key: string, value: string, scope?: string) => void;
  deleteRunTimeConfig: (key: string, scope?: string) => void;
  scope: string;
  universeUUID?: string;
  providerUUID?: string;
  customerUUID?: string;
}

export const ConfigData: FC<ConfigDataProps> = ({
  getRuntimeConfig,
  setRuntimeConfig,
  deleteRunTimeConfig,
  scope,
  universeUUID,
  providerUUID,
  customerUUID
}) => {
  const { t } = useTranslation();
  const [tagFilter, setTagFilter] = useState<string[]>(DEFAULT_RUNTIME_TAG_FILTER);
  const runtimeConfigs = useSelector((state: any) => state.customer.runtimeConfigs);
  const runtimeConfigsKeyMetadata = useSelector(
    (state: any) => state.customer.runtimeConfigsKeyMetadata
  );
  // Helps in deciding if the logged in user can mutate the config values
  const [searchText, setSearchText] = useState<string>('');
  const [editConfig, setEditConfig] = useState<boolean>(false);
  const [resetConfig, setResetConfig] = useState<boolean>(false);
  const [showOverridenValues, setShowOverridenValues] = useState<boolean>(false);
  const [listItems, setListItems] = useState([{ isConfigInherited: false }]);
  const [configData, setConfigData] = useState<RunTimeConfigData>({
    configID: 0,
    configKey: '',
    configValue: '',
    configTags: [],
    isConfigInherited: true,
    displayName: '',
    helpTxt: '',
    type: '',
    scope: ''
  });

  const runtimeConfigEntries = runtimeConfigs?.data?.configEntries;

  useEffect(() => {
    if (isNonEmptyArray(runtimeConfigEntries) && isNonEmptyArray(runtimeConfigsKeyMetadata?.data)) {
      const filteredConfigsMetadata = runtimeConfigsKeyMetadata.data.filter(
        (configKeyMetadata: any) =>
          configKeyMetadata.tags.some((tag: string) => tagFilter.includes(tag))
      );

      const runtimeConfigItems = filteredConfigsMetadata
        ?.map((configKeyMetadata: any, idx: number) => {
          return {
            displayName: configKeyMetadata.displayName,
            helpTxt: configKeyMetadata.helpTxt,
            type: configKeyMetadata.dataType?.name,
            scope: configKeyMetadata.scope,
            configKey: configKeyMetadata.key,
            configID: idx + 1,
            configTags: configKeyMetadata.tags
          };
        })
        ?.filter((item: any) => {
          return runtimeConfigEntries?.find((entry: any) => {
            let isScopeValid = false;
            if (ConfigScopePriority[scope] === 1) {
              isScopeValid = true;
            } else if (ConfigScopePriority[scope] === 2 && ConfigScopePriority[item.scope] >= 2) {
              isScopeValid = true;
            } else if (ConfigScopePriority[scope] === 3 && ConfigScopePriority[item.scope] === 3) {
              isScopeValid = true;
            } else if (ConfigScopePriority[scope] === 4 && ConfigScopePriority[item.scope] === 4) {
              isScopeValid = true;
            }
            if (entry.key === item.configKey && isScopeValid) {
              item.configValue = entry.value;
              item.isConfigInherited = entry.inherited;
            }
            return entry.key === item.configKey && isScopeValid;
          });
        });
      setListItems(runtimeConfigItems);
    }
  }, [scope, universeUUID, providerUUID, customerUUID, runtimeConfigEntries, tagFilter]); // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    fetchUiTagFilter();
  }, [scope]);

  const fetchUiTagFilter = async () => {
    const uiTagFilterResponse: any = await getRuntimeConfig(
      'yb.runtime_conf_ui.tag_filter',
      DEFAULT_RUNTIME_GLOBAL_SCOPE
    );
    if (isNonEmptyArray(uiTagFilterResponse?.payload?.data)) {
      setTagFilter(uiTagFilterResponse?.payload?.data);
    }
  };

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
        <RbacValidator
          customValidateFunction={(userPerm) =>
            scope === RunTimeConfigScope.GLOBAL
              ? find(userPerm, { actions: [Action.SUPER_ADMIN_ACTIONS] }) !== undefined
              : hasNecessaryPerm(ApiPermissionMap.MODIFY_RUNTIME_CONFIG_BY_KEY)
          }
          isControl
        >
          <MenuItem
            onClick={() => {
              openEditConfig(row);
            }}
          >
            {t('admin.advanced.globalConfig.ModelEditConfigTitle')}
          </MenuItem>
        </RbacValidator>
        {!row.isConfigInherited && (
          <RbacValidator
            customValidateFunction={(userPerm) =>
              scope === RunTimeConfigScope.GLOBAL
                ? find(userPerm, { actions: [Action.SUPER_ADMIN_ACTIONS] }) !== undefined
                : hasNecessaryPerm(ApiPermissionMap.MODIFY_RUNTIME_CONFIG_BY_KEY)
            }
            isControl
            overrideStyle={{ display: 'block' }}
          >
            <MenuItem
              onClick={() => {
                openResetConfig(row);
              }}
            >
              {t('admin.advanced.globalConfig.ModelResetConfigTitle')}
            </MenuItem>
          </RbacValidator>
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

  const onSearchChange = (searchText: string) => {
    setSearchText(searchText);
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
        options={{
          clearSearch: false,
          onSearchChange: onSearchChange,
          defaultSearch: searchText
        }}
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
        <TableHeaderColumn
          dataField={'actions'}
          columnClassName={'yb-actions-cell'}
          width="10%"
          dataFormat={formatActionButtons}
        >
          Actions
        </TableHeaderColumn>
      </BootstrapTable>
      {editConfig && (
        <EditConfig
          configData={configData}
          onHide={() => setEditConfig(false)}
          setRuntimeConfig={setRuntimeConfig}
          fetchUiTagFilter={fetchUiTagFilter}
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
          fetchUiTagFilter={fetchUiTagFilter}
          scope={scope}
          universeUUID={universeUUID}
          providerUUID={providerUUID}
          customerUUID={customerUUID}
        />
      )}
    </div>
  );
};

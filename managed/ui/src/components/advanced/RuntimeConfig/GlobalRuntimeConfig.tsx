import { FC, useEffect } from 'react';
import { RunTimeConfigScope, RuntimeConfigScopeProps } from '../../../redesign/utils/dtos';
import { ConfigData } from '../ConfigData';

import '../AdvancedConfig.scss';

export const GlobalRuntimeConfig: FC<RuntimeConfigScopeProps> = ({
  configTagFilter,
  fetchRuntimeConfigs,
  setRuntimeConfig,
  deleteRunTimeConfig,
  resetRuntimeConfigs
}) => {
  useEffect(() => {
    resetRuntimeConfigs();
    fetchRuntimeConfigs();
  }, []); // eslint-disable-line react-hooks/exhaustive-deps

  return (
    <div className="global-runtime-config-container">
      <ConfigData
        setRuntimeConfig={setRuntimeConfig}
        deleteRunTimeConfig={deleteRunTimeConfig}
        scope={RunTimeConfigScope.GLOBAL}
        configTagFilter={configTagFilter}
      />
    </div>
  );
};

import { FC, useEffect } from 'react';
import { RunTimeConfigScope, RuntimeConfigScopeProps } from '../../../redesign/utils/dtos';
import { ConfigData } from '../ConfigData';

import '../AdvancedConfig.scss';

export const GlobalRuntimeConfig: FC<RuntimeConfigScopeProps> = ({
  getRuntimeConfig,
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
        getRuntimeConfig={getRuntimeConfig}
        setRuntimeConfig={setRuntimeConfig}
        deleteRunTimeConfig={deleteRunTimeConfig}
        scope={RunTimeConfigScope.GLOBAL}
      />
    </div>
  );
};

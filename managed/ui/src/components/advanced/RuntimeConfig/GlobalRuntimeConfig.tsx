import React, { FC, useEffect } from 'react';

import { RunTimeConfigScope } from '../../../redesign/helpers/dtos';
import { ConfigData } from '../ConfigData';

import '../AdvancedConfig.scss';

interface GlobalRuntimeConfigProps {
  fetchRuntimeConfigs: (scope?: string) => void;
  setRuntimeConfig: (key: string, value: string) => void;
  deleteRunTimeConfig: (key: string) => void;
  resetRuntimeConfigs: () => void;
}

export const GlobalRuntimeConfig: FC<GlobalRuntimeConfigProps> = ({
  fetchRuntimeConfigs,
  setRuntimeConfig,
  deleteRunTimeConfig,
  resetRuntimeConfigs,
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
      />
    </div>
  );
}

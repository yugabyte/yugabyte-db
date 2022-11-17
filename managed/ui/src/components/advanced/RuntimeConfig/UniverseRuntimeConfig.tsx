import React, { FC, useEffect, useState } from 'react';
import { MenuItem, Dropdown } from 'react-bootstrap';
import { useQuery } from 'react-query';

import { fetchUniversesList } from '../../../actions/xClusterReplication';
import { RunTimeConfigScope } from '../../../redesign/helpers/dtos';
import { ConfigData } from '../ConfigData';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';

import '../AdvancedConfig.scss';

interface UniverseRuntimeConfigProps {
  fetchRuntimeConfigs: (scope?: string) => void;
  setRuntimeConfig: (key: string, value: string, scope?: string) => void;
  deleteRunTimeConfig: (key: string, scope?: string) => void;
  resetRuntimeConfigs: () => void;
}

export const UniverseRuntimeConfig: FC<UniverseRuntimeConfigProps> = ({
  fetchRuntimeConfigs,
  setRuntimeConfig,
  deleteRunTimeConfig,
  resetRuntimeConfigs,
}) => {
  // const universesList = useSelector((state: any) => state.universe.universeList);
  const universes = useQuery(['universes'], () =>
    fetchUniversesList().then((res: any) => res.data)
  );
  const [universeDropdownValue, setUniverseDropdownValue] = useState<string>();
  const [universeUUID, setUniverseUUID] = useState<string>();

  const onUniverseDropdownChanged = (universeName: string, universeUUID: string) => {
    setUniverseDropdownValue(universeName);
    setUniverseUUID(universeUUID);
  }

  useEffect(() => {
    resetRuntimeConfigs();
    if (universeUUID) {
      fetchRuntimeConfigs(universeUUID);
    }
  }, [universeUUID]); // eslint-disable-line react-hooks/exhaustive-deps

  if (universes.isError) {
    return (
      <YBErrorIndicator customErrorMessage="Please try again" />
    );
  }
  if (universes.isLoading || (universes.isIdle && universes.data === undefined)) {
    return <YBLoading />
  }

  const universesList = universes.data;
  if (universesList.length > 0 && universeDropdownValue === undefined) {
    setUniverseDropdownValue(universesList[0].name);
  }
  if (universesList.length > 0 && universeUUID === undefined) {
    setUniverseUUID(universesList[0].universeUUID);
  }

  return (
    <div className="universe-runtime-config-container">
      <div className="universe-runtime-config-container__display">
        <span className="universe-runtime-config-container__label"> {"Select Universe:"}</span>
        &nbsp;&nbsp;
        <Dropdown
          id="universeRuntimeConfigDropdown"
          className="universe-runtime-config-dropdown"
        >
          <Dropdown.Toggle>
            <span className="universe-config-dropdown-value">{universeDropdownValue}</span>
          </Dropdown.Toggle>
          <Dropdown.Menu>
            {universesList?.length > 0 &&
              universesList.map((universe: any, universeIdx: number) => {
                return (
                  <MenuItem
                    eventKey={`universe-${universeIdx}`}
                    key={`${universe.universeUUID}`}
                    active={universeDropdownValue === universe.name}
                    onSelect={() => onUniverseDropdownChanged?.(universe.name, universe.universeUUID)}
                  >
                    {universe.name}
                  </MenuItem>
                );
              })
            }
          </Dropdown.Menu>
        </Dropdown>
      </div>

      <ConfigData
        setRuntimeConfig={setRuntimeConfig}
        deleteRunTimeConfig={deleteRunTimeConfig}
        scope={RunTimeConfigScope.UNIVERSE}
        universeUUID={universeUUID}
      />
    </div>
  );
}

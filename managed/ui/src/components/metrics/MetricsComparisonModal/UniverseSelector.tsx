import React, { FC } from 'react';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { MenuItem, Dropdown } from 'react-bootstrap';
import { fetchUniversesList } from '../../../actions/xClusterReplication';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { MetricConsts } from '../constants';

interface UniverseSelectorProps {
  onUniverseChanged: (universe: any) => void;
  currentSelectedUniverse: string;
  currentSelectedUniverseUUID: string;
}

export const UniverseSelector: FC<UniverseSelectorProps> = ({
  onUniverseChanged,
  currentSelectedUniverse,
  currentSelectedUniverseUUID
}) => {
  const { t } = useTranslation();
  let universeItems: any = [];

  const universes = useQuery(['universes'], () =>
    fetchUniversesList().then((res: any) => res.data)
  );

  if (universes.isError) {
    return <YBErrorIndicator customErrorMessage={t('universeSelector.errorMessage')} />;
  }

  if (universes.isLoading) {
    return <YBLoading />;
  }

  const universesList = universes.data;
  // eslint-disable-next-line react/display-name
  universeItems = universesList?.map((universe: any, universeIdx: number) => {
    return (
      <MenuItem
        key={`universe-${universeIdx}`}
        onSelect={() => onUniverseChanged(universe)}
        eventKey={`universe-${universeIdx}`}
        active={
          universe.name === currentSelectedUniverse &&
          universe.universeUUID === currentSelectedUniverseUUID
        }
      >
        <span className="universe-name">{universe.name}</span>
      </MenuItem>
    );
  });

  // By default we need to have 'All regions' populated
  const defaultMenuItem = (
    <MenuItem
      onSelect={() => onUniverseChanged(MetricConsts.ALL)}
      key={MetricConsts.ALL}
      active={currentSelectedUniverse === MetricConsts.ALL}
      eventKey={MetricConsts.ALL}
    >
      {t('universeSelector.all')}
    </MenuItem>
  );
  universeItems.splice(0, 0, defaultMenuItem);

  return (
    <div className="universe-picker-container pull-left">
      <Dropdown id="universeFilterDropdown" className="universe-filter-dropdown">
        <Dropdown.Toggle className="dropdown-toggle-button">
          <span className="default-value">
            {currentSelectedUniverse === MetricConsts.ALL
              ? universeItems[0]
              : currentSelectedUniverse}
          </span>
        </Dropdown.Toggle>
        <Dropdown.Menu>{universeItems.length > 1 && universeItems}</Dropdown.Menu>
      </Dropdown>
    </div>
  );
};

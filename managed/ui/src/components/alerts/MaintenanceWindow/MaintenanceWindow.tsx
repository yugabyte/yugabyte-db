import { FC, useState } from 'react';
import { useQuery } from 'react-query';
import { MaintenanceWindowSchema } from '.';
import { fetchUniversesList } from '../../../actions/xClusterReplication';
import { YBLoading } from '../../common/indicators';
import { CreateMaintenanceWindow } from './CreateMaintenanceWindow';

import { MaintenanceWindowsList } from './MaintenanceWindowsList';

// eslint-disable-next-line @typescript-eslint/no-unused-vars
enum VIEW_STATES {
  CREATE,
  LIST
}

export const MaintenanceWindow: FC = () => {
  const [currentView, setCurrentView] = useState<VIEW_STATES>(VIEW_STATES.LIST);

  const [selectedWindow, setSelectedWindow] = useState<MaintenanceWindowSchema | null>(null);

  const { data: universeList, isLoading: isUniverseListLoading } = useQuery(['universeList'], () =>
    fetchUniversesList().then((res) => res.data)
  );

  if (isUniverseListLoading) {
    return <YBLoading />;
  }

  if (currentView === VIEW_STATES.CREATE) {
    return (
      <CreateMaintenanceWindow
        universeList={universeList}
        showListView={() => {
          setCurrentView(VIEW_STATES.LIST);
        }}
        selectedWindow={selectedWindow}
      />
    );
  }

  return (
    <MaintenanceWindowsList
      universeList={universeList}
      showCreateView={() => {
        setCurrentView(VIEW_STATES.CREATE);
      }}
      setSelectedWindow={(selectedWindow) => {
        setSelectedWindow(selectedWindow);
        setCurrentView(VIEW_STATES.CREATE);
      }}
    />
  );
};

import { ChangeEvent, FC, useState } from 'react';
import { Col, DropdownButton, MenuItem, OverlayTrigger, Popover, Row } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import {
  convertUTCStringToMoment,
  deleteMaintenanceWindow,
  getMaintenanceWindowList,
  MaintenanceWindowSchema,
  MaintenanceWindowState,
  updateMaintenanceWindow
} from '.';
import {
  convertToISODateString,
  dateStrToMoment,
  ybFormatDate
} from '../../../redesign/helpers/DateUtils';
import { YBButton, YBCheckBox } from '../../common/forms/fields';
import { YBLoading } from '../../common/indicators';
import { YBConfirmModal } from '../../modals';

import { RbacValidator } from '../../../redesign/features/rbac/common/RbacValidator';
import { UserPermissionMap } from '../../../redesign/features/rbac/UserPermPathMapping';
import './MaintenanceWindowsList.scss';

/**
 * Extend time frame options in minutes
 */
const extendTimeframes = {
  '30 Minutes': 30,
  '1 hour': 60,
  '2 hours': 120,
  '6 hours': 360
};

const statusMap: Record<MaintenanceWindowState, string> = {
  FINISHED: 'Completed',
  ACTIVE: 'Active',
  PENDING: 'Scheduled'
};

interface MaintenanceWindowsListProps {
  universeList: { universeUUID: string; name: string }[];
  showCreateView: () => void;
  setSelectedWindow: (window: MaintenanceWindowSchema | null) => void;
}

/**
 * Calculates the difference between two dates
 * @param startTime start time
 * @param endtime end time
 * @returns diff between the dates
 */
const calculateDuration = (startTime: string, endtime: string): string => {
  const start = convertUTCStringToMoment(startTime);
  const end = convertUTCStringToMoment(endtime);

  const totalDays = end.diff(start, 'days');
  const totalHours = end.diff(start, 'hours');
  const totalMinutes = end.diff(start, 'minutes');
  let duration = totalDays !== 0 ? `${totalDays}d ` : '';
  duration += totalHours % 24 !== 0 ? `${totalHours % 24}h ` : '';
  duration += totalMinutes % 60 !== 0 ? `${totalMinutes % 60}m` : '';
  return duration;
};

const getStatusTag = (status: MaintenanceWindowSchema['state']) => {
  return <span className={`state-tag ${status}`}>{statusMap[status]}</span>;
};

/**
 * return the action dom for the maintenance window table
 */
const GetMaintenanceWindowActions = ({
  currentWindow,
  setSelectedWindow
}: {
  currentWindow: MaintenanceWindowSchema;
  setSelectedWindow: MaintenanceWindowsListProps['setSelectedWindow'];
}) => {
  const queryClient = useQueryClient();

  const extendTime = useMutation(
    ({ window, minutesToExtend }: { window: MaintenanceWindowSchema; minutesToExtend: number }) => {
      const currentEndTime = dateStrToMoment(window.endTime).add(minutesToExtend, 'minute');
      return updateMaintenanceWindow({
        ...window,
        endTime: convertToISODateString(currentEndTime.toDate())
      });
    },
    {
      onSuccess: () => queryClient.invalidateQueries('maintenenceWindows')
    }
  );

  const markAsCompleted = useMutation(
    (window: MaintenanceWindowSchema) =>
      updateMaintenanceWindow({ ...window, endTime: convertToISODateString(new Date()) }),
    {
      onSuccess: () => queryClient.invalidateQueries('maintenenceWindows')
    }
  );

  const deleteWindow = useMutation((uuid: string) => deleteMaintenanceWindow(uuid), {
    onSuccess: () => queryClient.invalidateQueries('maintenenceWindows')
  });

  const [visibleModal, setVisibleModal] = useState<string | null>(null);
  return (
    <div className="maintenance-window-action">
      {/* Extend Options */}
      {currentWindow.state !== MaintenanceWindowState.FINISHED && (
        <DropdownButton
          className="extend-actions btn btn-default"
          title="Extend"
          id="bg-extented-dropdown"
          pullRight
        >
          {Object.keys(extendTimeframes).map((timeframe) => (
            <RbacValidator
              accessRequiredOn={{
                ...UserPermissionMap.editMaintenanceWindow
              }}
              isControl
              overrideStyle={{ display: 'block' }}
            >
              <MenuItem
                key={timeframe}
                onClick={() => {
                  extendTime.mutateAsync({
                    window: currentWindow,
                    minutesToExtend: extendTimeframes[timeframe]
                  });
                }}
              >
                {timeframe}
              </MenuItem>
            </RbacValidator>
          ))}
        </DropdownButton>
      )}
      {/* Actions */}
      <DropdownButton
        className="actions-btn"
        title="..."
        id="bg-actions-dropdown"
        noCaret
        pullRight
      >
        {currentWindow.state === MaintenanceWindowState.ACTIVE && (
          <RbacValidator
            accessRequiredOn={{
              ...UserPermissionMap.editMaintenanceWindow
            }}
            isControl
            overrideStyle={{ display: 'block' }}
          >
            <MenuItem onClick={() => markAsCompleted.mutateAsync(currentWindow)}>
              <i className="fa fa-check" /> Mark as Completed
            </MenuItem>
          </RbacValidator>
        )}
        {currentWindow.state !== MaintenanceWindowState.FINISHED && (
          <RbacValidator
            accessRequiredOn={{
              ...UserPermissionMap.editMaintenanceWindow
            }}
            isControl
            overrideStyle={{ display: 'block' }}
          >
            <MenuItem
              onClick={() => {
                setSelectedWindow(currentWindow);
              }}
            >
              <i className="fa fa-pencil" /> Edit Window
            </MenuItem>
          </RbacValidator>
        )}
        <RbacValidator
          accessRequiredOn={{
            ...UserPermissionMap.deleteMaintenanceWindow
          }}
          isControl
          overrideStyle={{ display: 'block' }}
        >
          <MenuItem
            onClick={() => {
              setVisibleModal(currentWindow?.uuid);
            }}
          >
            <i className="fa fa-trash-o" /> Delete Window
          </MenuItem>
        </RbacValidator>
      </DropdownButton>
      <YBConfirmModal
        name="delete-alert-config"
        title="Confirm Delete"
        onConfirm={() => deleteWindow.mutateAsync(currentWindow.uuid)}
        currentModal={currentWindow?.uuid}
        visibleModal={visibleModal}
        hideConfirmModal={() => {
          setVisibleModal(null);
        }}
      >
        {`Are you sure you want to delete "${currentWindow?.name}" maintenance window?`}
      </YBConfirmModal>
    </div>
  );
};

const getTargetUniverse = (universeNames: string[]) => {
  if (universeNames.length === 1) return universeNames[0];
  const popover = (
    <Popover id="more-universe-list">
      {universeNames.slice(1).map((name) => (
        <div key={name} className="universe-name">
          {name}
        </div>
      ))}
    </Popover>
  );
  return (
    <>
      {universeNames[0]},&nbsp;
      <OverlayTrigger overlay={popover} trigger="click" placement="bottom">
        <span className="more-universe-list">+{universeNames.length - 1}</span>
      </OverlayTrigger>
    </>
  );
};

export const MaintenanceWindowsList: FC<MaintenanceWindowsListProps> = ({
  universeList,
  showCreateView,
  setSelectedWindow
}) => {
  const { data, isFetching: isMaintenanceWindowsListFetching } = useQuery(
    ['maintenenceWindows'],
    () => getMaintenanceWindowList()
  );

  const [showCompletedWindows, setShowCompletedWindows] = useState(false);

  const maintenenceWindows = !showCompletedWindows
    ? data?.filter((window) => window.state !== MaintenanceWindowState.FINISHED)
    : data;

  const findUniverseNamesByUUIDs = (uuids: string[]) => {
    return universeList
      .filter((universe) => uuids.includes(universe.universeUUID))
      .map((universe) => universe.name);
  };

  return (
    <div className="maintenance-windows">
      <Row className="header">
        <Col lg={10}>
          <YBCheckBox
            checkState={showCompletedWindows}
            field={{
              onChange: (e: ChangeEvent<HTMLInputElement>) =>
                setShowCompletedWindows(e.target.checked)
            }}
            label={<span className="checkbox-label">Show Completed Maintenance</span>}
          />
        </Col>
        <Col lg={2}>
          <RbacValidator
            accessRequiredOn={{
              ...UserPermissionMap.createMaintenenceWindow
            }}
            isControl
          >
            <YBButton
              btnText="Add Maintenance Window"
              btnClass="btn btn-orange"
              onClick={() => {
                setSelectedWindow(null);
                showCreateView();
              }}
            />
          </RbacValidator>
        </Col>
      </Row>
      <Row>
        <Col lg={12} id="maintenance-window-table">
          {isMaintenanceWindowsListFetching && <YBLoading />}

          {maintenenceWindows && (
            <BootstrapTable data={maintenenceWindows} pagination={true}>
              <TableHeaderColumn dataField="uuid" isKey={true} hidden={true} />
              <TableHeaderColumn
                dataField="name"
                columnClassName="no-border"
                className="no-border"
                dataAlign="left"
                width={'30%'}
                dataSort
              >
                name
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="startTime"
                columnClassName="no-border"
                className="no-border"
                dataAlign="left"
                width={'20%'}
                dataFormat={(cell) => ybFormatDate(cell)}
                dataSort
              >
                Start Time
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="endTime"
                columnClassName="no-border"
                className="no-border"
                dataAlign="left"
                width={'20%'}
                dataFormat={(cell) => ybFormatDate(cell)}
                dataSort
              >
                End Time
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="targetUniverse"
                dataAlign="left"
                width={'20%'}
                dataFormat={(_, row: MaintenanceWindowSchema) => {
                  if (row.alertConfigurationFilter?.target?.all) {
                    return 'ALL';
                  }
                  const universes = findUniverseNamesByUUIDs(
                    row.alertConfigurationFilter.target.uuids
                  );
                  // if the universe are deleted return empty string
                  return universes.length === 0 ? '' : getTargetUniverse(universes);
                }}
              >
                Target Universe
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="duration"
                columnClassName="no-border"
                className="no-border"
                dataAlign="left"
                width={'10%'}
                dataFormat={(_, row) => calculateDuration(row.startTime, row.endTime)}
                dataSort
                sortFunc={(
                  a: MaintenanceWindowSchema,
                  b: MaintenanceWindowSchema,
                  order: string
                ) => {
                  const t1 = convertUTCStringToMoment(a.endTime).diff(
                    convertUTCStringToMoment(a.startTime)
                  );
                  const t2 = convertUTCStringToMoment(b.endTime).diff(
                    convertUTCStringToMoment(b.startTime)
                  );
                  return order === 'asc' ? t1 - t2 : t2 - t1;
                }}
              >
                Duration
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="state"
                columnClassName="no-border"
                className="no-border"
                dataAlign="left"
                width={'10%'}
                dataFormat={(cell) => getStatusTag(cell)}
                dataSort
              >
                Status
              </TableHeaderColumn>
              <TableHeaderColumn
                dataField="actions"
                dataAlign="left"
                width={'12%'}
                dataFormat={(_, row) => (
                  <GetMaintenanceWindowActions
                    currentWindow={row}
                    setSelectedWindow={setSelectedWindow}
                  />
                )}
                columnClassName="yb-actions-cell no-border"
              >
                Actions
              </TableHeaderColumn>
            </BootstrapTable>
          )}
        </Col>
      </Row>
    </div>
  );
};

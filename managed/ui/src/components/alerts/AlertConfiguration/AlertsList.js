// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nishant.sharma@hashedin.com)
//
// This file will hold all the configuration list of alerts.

import { useEffect, useRef, useState } from 'react';
import { find } from 'lodash';
import { Col, DropdownButton, MenuItem, Row } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { isNonEmptyArray } from '../../../utils/ObjectUtils';
import { FlexContainer, FlexShrink } from '../../common/flexbox/YBFlexBox';
import { YBLoading } from '../../common/indicators';
import { YBConfirmModal } from '../../modals';
import { YBPanelItem } from '../../panels';
import { isNonAvailable } from '../../../utils/LayoutUtils';
import {
  AlertListAsPill,
  AlertListsWithFilter,
  DEFAULT_DESTINATION,
  FILTER_TYPE_DESTINATION,
  FILTER_TYPE_METRIC_NAME,
  FILTER_TYPE_NAME,
  FILTER_TYPE_SEVERITY,
  FILTER_TYPE_STATE,
  FILTER_TYPE_TARGET_TYPE,
  FILTER_TYPE_UNIVERSE,
  formatString,
  NO_DESTINATION
} from './AlertsFilter';
import { ybFormatDate } from '../../../redesign/helpers/DateUtils';
import {
  RbacValidator,
  customPermValidateFunction,
  hasNecessaryPerm
} from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { Action, Resource } from '../../../redesign/features/rbac';
import { ControlComp } from '../../../redesign/features/rbac/common/validator/ValidatorUtils';

/**
 * This is the header for YB Panel Item.
 */
const header = (
  isReadOnly,
  onCreateAlert,
  enablePlatformAlert,
  handleMetricsCall,
  setInitialValues,
  filterVisible,
  setFilterVisible,
  alertsFilters,
  updateFilters,
  clearAllFilters
) => {
  const canCreatePolicy = hasNecessaryPerm(ApiPermissionMap.CREATE_ALERT_CONFIGURATIONS);
  return (
    <>
      <Row className="pills-container">
        <Col lg={10}>
          <span onClick={() => setFilterVisible(!filterVisible)} className="toggle-filter">
            <i className="fa fa-sliders" aria-hidden="true"></i> {filterVisible ? 'Hide ' : ''}
            Filter
          </span>
          {!filterVisible && Object.keys(alertsFilters).length !== 0 ? (
            <AlertListAsPill
              clearAllFilters={clearAllFilters}
              alertsFilters={alertsFilters}
              updateFilters={updateFilters}
            />
          ) : null}
        </Col>
        <Col lg={2}>
          <FlexContainer className="pull-right">
            <FlexShrink>
              {!isReadOnly && (
                <DropdownButton
                  className="alert-config-actions btn btn-orange"
                  title="Create Alert Policy"
                  id="bg-nested-dropdown"
                  bsStyle="danger"
                  pullRight
                >
                  <RbacValidator
                    accessRequiredOn={ApiPermissionMap.CREATE_ALERT_CONFIGURATIONS}
                    isControl
                  >
                    <MenuItem
                      className="alert-config-list"
                      onClick={() => {
                        handleMetricsCall('UNIVERSE');
                        onCreateAlert(true);
                        enablePlatformAlert(false);
                        setInitialValues({ ALERT_TARGET_TYPE: 'allUniverses' });
                      }}
                      data-testid="Create-Universe-Alert"
                    >
                      <i className="fa fa-globe"></i> Universe Alert
                    </MenuItem>
                  </RbacValidator>

                  <RbacValidator
                    accessRequiredOn={ApiPermissionMap.CREATE_ALERT_CONFIGURATIONS}
                    isControl
                  >
                    <MenuItem
                      className="alert-config-list"
                      onClick={() => {
                        handleMetricsCall('PLATFORM');
                        onCreateAlert(true);
                        enablePlatformAlert(true);
                        setInitialValues({ ALERT_TARGET_TYPE: 'allUniverses' });
                      }}
                      data-testid="Create-Platform-Alert"
                    >
                      <i className="fa fa-clone tab-logo" aria-hidden="true"></i> Platform Alert
                    </MenuItem>
                  </RbacValidator>
                </DropdownButton>
              )}
            </FlexShrink>
          </FlexContainer>
        </Col>
      </Row>
    </>
  );
};

/**
 * Request payload to get the list of alerts.
 */
const payload = {
  uuids: [],
  name: null,
  targetUuid: null,
  routeUuid: null
};

const getTag = (type) => {
  return <span className="name-tag">{type}</span>;
};

const NO_DESTINATION_MSG = (
  <span className="no-destination-msg">
    <i className="fa fa-exclamation-triangle" aria-hidden="true" /> No Destination
  </span>
);

export const AlertsList = (props) => {
  const [alertList, setAlertList] = useState([]);
  const [metrics, setMetrics] = useState([]);
  const [alertDestinationList, setAlertDestinationList] = useState([]);
  const [defaultDestination, setDefaultDestination] = useState(undefined);
  const [filterVisible, setFilterVisible] = useState(true);
  const [filters, setFilters] = useState({});
  const [isAlertListLoading, setIsAlertListLoading] = useState(false);
  const bootstrapTableRef = useRef(null);

  const {
    customer,
    alertConfigs,
    alertUniverseList,
    alertDestinations,
    closeModal,
    deleteAlertConfig,
    enablePlatformAlert,
    handleMetricsCall,
    modal: { visibleModal },
    onCreateAlert,
    showDeleteModal,
    setInitialValues,
    universes,
    updateAlertConfig,
    sendTestAlert
  } = props;
  const [sizePerPage, setSizePerPage] = useState(10);
  const [options, setOptions] = useState({
    noDataText: 'Loading...',
    defaultSortName: 'name',
    defaultSortOrder: 'asc',
    page: 1
  });

  const isReadOnly = isNonAvailable(customer.data.features, 'alert.configuration.actions');

  const onInit = () => {
    alertConfigs(payload).then((res) => {
      setAlertList(res);
      setMetrics(res);
      setOptions({ ...options, noDataText: 'There is no data to display ' });
    });

    alertDestinations().then((res) => {
      setDefaultDestination(res?.find((destination) => destination.defaultDestination));
      setAlertDestinationList(res);
    });
  };

  useEffect(onInit, []);

  const formatName = (cell, row) => {
    const Tag = getTag(row.targetType);

    let name = row.name;

    if (filters[FILTER_TYPE_NAME] && !isAlertListLoading) {
      const searchText = filters[FILTER_TYPE_NAME];
      const index = row.name.toLowerCase().indexOf(searchText.toLowerCase());

      name = (
        <span>
          {row?.name?.substr(0, index)}
          <span className="highlight">{row?.name?.substr(index, searchText.length)}</span>
          {row?.name?.substr(index + searchText.length)}
        </span>
      );
    }

    if (!row.active) {
      return (
        <span className="alert-inactive" title={row.name}>
          <span className="alert-name">{name}</span> (Inactive) {Tag}
        </span>
      );
    }
    return (
      <span title={row.name}>
        <span className="alert-name">{name}&nbsp;</span> {Tag}
      </span>
    );
  };

  /**
   * This method is used to congifure the destinationUUID with its repsective
   * destination name.
   *
   * @param {string} cell Not in-use.
   * @param {object} row Respective details
   */
  const formatRoutes = (cell, row) => {
    if (row.defaultDestination) {
      const tag = getTag('DEFAULT');
      return (
        <span>
          {' '}
          {defaultDestination?.name} {tag}
        </span>
      );
    }
    const route = alertDestinationList
      .map((destination) => {
        return destination.uuid === row.destinationUUID ? destination.name : null;
      })
      .filter((res) => res !== null);

    if (route.length > 0) {
      return route;
    }
    return NO_DESTINATION_MSG;
  };

  /**
   * This method is used to get the severity from thresholds object.
   *
   * @param {string} cell Not in-use.
   * @param {object} row Respective details.
   */
  const formatThresholds = (cell, row) => {
    return Object.keys(row.thresholds)
      .map((severity) => formatString(severity))
      .sort()
      .join(', ');
  };

  /**
   * This method will return the date in format.
   *
   * @param {string} cell Not in-use.
   * @param {object} row Respective row.
   */
  const formatCreatedTime = (cell, row) => {
    return ybFormatDate(row.createTime);
  };

  /**
   * This method will help us to edit the details for a respective row.
   *
   * @param {object} row Respective row object.
   */
  const onEditAlertConfig = (row) => {
    row.targetType === 'PLATFORM' ? enablePlatformAlert(true) : enablePlatformAlert(false);

    // setting up ALERT_DESTINATION_LIST.
    const destination = alertDestinationList
      .map((destination) => {
        return destination.uuid === row.destinationUUID
          ? {
              value: destination.uuid,
              label: destination.name
            }
          : null;
      })
      .filter((res) => res !== null);

    // setting up ALERT_METRICS_CONDITION_POLICY.
    const condition = Object.keys(row.thresholds).map((key) => {
      return {
        _SEVERITY: key,
        _CONDITION: row.thresholds[key].condition,
        _THRESHOLD: row.thresholds[key].threshold
      };
    });

    // setting up ALERT_TARGET_TYPE & ALERT_UNIVERSE_LIST.
    const currentDestination = destination[0]?.value
      ? destination[0]?.value
      : row.defaultDestination
      ? '<default>'
      : '<empty>';
    const targetType = row.target.all ? 'allUniverses' : 'selectedUniverses';
    const univerList =
      isNonEmptyArray(row.target.uuids) &&
      row.target.uuids.map((list) => alertUniverseList.find((universe) => universe.value === list));
    // setting up the initial values.
    const initialVal = {
      type: 'update',
      uuid: row.uuid,
      createTime: row.createTime,
      ALERT_CONFIGURATION_NAME: row.name,
      ALERT_CONFIGURATION_DESCRIPTION: row.description,
      ALERT_TARGET_TYPE: targetType,
      ALERT_UNIVERSE_LIST: univerList,
      ALERT_METRICS_CONDITION: row.template,
      ALERT_METRICS_DURATION: row.durationSec ? row.durationSec : '0',
      ALERT_METRICS_CONDITION_POLICY: condition,
      ALERT_DESTINATION_LIST: currentDestination,
      thresholdUnit: row.thresholdUnit,
      ALERT_STATUS: row.active
    };

    setInitialValues(initialVal);
    onCreateAlert(true);
  };

  const onToggleActive = (row) => {
    const updatedAlertConfig = { ...row, active: !row.active };
    updateAlertConfig(updatedAlertConfig, row.uuid).then(() => {
      const reqPayload = preparePayloadForAlertReq();
      alertConfigs(reqPayload).then((res) => {
        setAlertList(res);
      });
    });
  };

  const onSendTestAlert = (row) => {
    sendTestAlert(row.uuid);
  };
  /**
   * This method will help us to delete the respective row record.
   *
   * @param {object} row Respective row data.
   */
  const onDeleteConfig = (row) => {
    deleteAlertConfig(row.uuid).then(() => {
      const reqPayload = preparePayloadForAlertReq();
      alertConfigs(reqPayload).then((res) => {
        setAlertList(res);
      });
    });
  };

  const formatAlertTargets = (cell) => {
    if (cell.all) return 'All';
    if (
      !customPermValidateFunction((userPermissions) => {
        return (
          find(userPermissions, { resourceType: Resource.UNIVERSE, actions: Action.READ }) !==
          undefined
        );
      })
    ) {
      return ControlComp({ children: <span>No Universe Perm</span> });
    }
    const targetUniverse = cell.uuids
      .map((uuid) => {
        return universes.data.find((destination) => destination.universeUUID === uuid);
      })
      .filter(Boolean) //filtering undefined, if the universe is already deleted
      .sort();

    return (
      <span>
        {targetUniverse.map((u) => (
          <div key={u.universeUUID} title={u.name}>
            {u.name}
          </div>
        ))}
      </span>
    );
  };

  const targetSortFunc = (a, b, order) => {
    const targetA = getTargetString(a.target);
    const targetB = getTargetString(b.target);
    if (order === 'desc') {
      return targetB.localeCompare(targetA);
    } else {
      return targetA.localeCompare(targetB);
    }
  };

  const getTargetString = (target) => {
    if (target.all) return 'ALL';
    return target.uuids
      .map((uuid) => {
        return universes.data.find((destination) => destination.universeUUID === uuid);
      })
      .filter(Boolean) //filtering undefined, if the universe is already deleted
      .sort()
      .join();
  };

  const thresholdSortFunc = (a, b, order) => {
    const severitiesA = getSeveritiesString(a.thresholds);
    const severitiesB = getSeveritiesString(b.thresholds);
    if (order === 'desc') {
      return severitiesB.localeCompare(severitiesA);
    } else {
      return severitiesA.localeCompare(severitiesB);
    }
  };

  const getSeveritiesString = (thresholds) => {
    return Object.keys(thresholds)
      .map((severity) => severity)
      .sort()
      .join();
  };

  const destinationsSortFunc = (a, b, order) => {
    const destinationA = getDestinationsString(a);
    const destinationB = getDestinationsString(b);
    if (order === 'desc') {
      return destinationB.localeCompare(destinationA);
    } else {
      return destinationA.localeCompare(destinationB);
    }
  };

  const getDestinationsString = (config) => {
    if (config.defaultDestination) {
      return 'Use Default';
    }
    const destination = alertDestinationList
      .map((destination) => {
        return destination.uuid === config.destinationUUID ? destination.name : null;
      })
      .filter((res) => res !== null)
      .join();

    if (destination) {
      return destination;
    }
    return 'No destination';
  };

  const decideDropdownMenuPos = (rowIndex, sizePerPage, totalRecords, currentPage) => {
    //display the menu at the bottom for the top five records
    if (rowIndex < 5) {
      return false;
    }
    //display the menu at the top for the last five records
    const itemsPresentInCurrentPage = Math.min(
      sizePerPage,
      totalRecords - currentPage * sizePerPage
    );

    return itemsPresentInCurrentPage - rowIndex < 5;
  };

  // This method will handle all the required actions for the particular row.
  const editActionLabel = isReadOnly ? 'Alert Details' : 'Edit Alert';
  const formatConfigActions = (cell, row, rowIndex, sizePerPage, totalRecords, currentPage) => {
    const canEditAlerts = hasNecessaryPerm(ApiPermissionMap.MODIFY_ALERT_CONFIGURATIONS);
    const canDeleteAlerts = hasNecessaryPerm(ApiPermissionMap.DELETE_ALERT_CONFIGURATIONS);

    return (
      <>
        <DropdownButton
          className="actions-dropdown"
          title="..."
          noCaret
          id="bg-nested-dropdown"
          dropup={decideDropdownMenuPos(rowIndex, sizePerPage, totalRecords, currentPage)}
          pullRight
        >
          <RbacValidator
            accessRequiredOn={ApiPermissionMap.MODIFY_ALERT_CONFIGURATIONS}
            isControl
            overrideStyle={{ display: 'block' }}
          >
            <MenuItem
              onClick={() => {
                if (!canEditAlerts) return;
                handleMetricsCall(row.targetType);
                onEditAlertConfig(row);
              }}
              disabled={!canEditAlerts}
            >
              <i className="fa fa-pencil"></i> {editActionLabel}
            </MenuItem>
          </RbacValidator>

          {!row.active && !isReadOnly ? (
            <RbacValidator
              accessRequiredOn={ApiPermissionMap.MODIFY_ALERT_CONFIGURATIONS}
              isControl
              overrideStyle={{ display: 'block' }}
            >
              <MenuItem
                onClick={() => {
                  onToggleActive(row);
                }}
              >
                <i className="fa fa-toggle-on"></i> Activate
              </MenuItem>
            </RbacValidator>
          ) : null}

          {row.active && !isReadOnly ? (
            <RbacValidator
              accessRequiredOn={ApiPermissionMap.MODIFY_ALERT_CONFIGURATIONS}
              isControl
              overrideStyle={{ display: 'block' }}
            >
              <MenuItem
                onClick={() => {
                  if (!canEditAlerts) return;
                  onToggleActive(row);
                }}
                disabled={!canEditAlerts}
              >
                <i className="fa fa-toggle-off"></i> Deactivate
              </MenuItem>
            </RbacValidator>
          ) : null}

          {!isReadOnly ? (
            <RbacValidator
              accessRequiredOn={ApiPermissionMap.DELETE_ALERT_CONFIGURATIONS}
              isControl
              overrideStyle={{ display: 'block' }}
            >
              <MenuItem
                onClick={() => {
                  if (!canDeleteAlerts) return;
                  showDeleteModal(row?.uuid);
                }}
                disabled={!canDeleteAlerts}
              >
                <i className="fa fa-trash"></i> Delete Alert
              </MenuItem>
            </RbacValidator>
          ) : null}

          {!isReadOnly ? (
            <RbacValidator
              accessRequiredOn={ApiPermissionMap.SEND_TEST_ALERT}
              isControl
              overrideStyle={{ display: 'block' }}
            >
              <MenuItem onClick={() => onSendTestAlert(row)}>
                <i className="fa fa-paper-plane"></i> Send Test Alert
              </MenuItem>
            </RbacValidator>
          ) : null}
        </DropdownButton>
        <YBConfirmModal
          name="delete-alert-config"
          title="Confirm Delete"
          onConfirm={() => onDeleteConfig(row)}
          currentModal={row?.uuid}
          visibleModal={visibleModal}
          hideConfirmModal={closeModal}
        >
          Are you sure you want to delete {row?.name} Alert Config?
        </YBConfirmModal>
      </>
    );
  };

  const updateFilters = (groupType, value, type, remove) => {
    switch (type) {
      case 'text':
      case 'select':
        if (!value || remove) {
          delete filters[groupType];
        } else {
          filters[groupType] = value;
        }
        break;
      case 'multiselect':
        if (!filters[groupType]) {
          filters[groupType] = [];
        }
        if (filters[groupType].includes(value)) {
          filters[groupType] = filters[groupType].filter((val) => val !== value);
          if (filters[groupType].length === 0) {
            delete filters[groupType];
          }
          // Remove universe , if "universe" is removed in target universe
          if (groupType === FILTER_TYPE_TARGET_TYPE && value === 'Universe') {
            delete filters[FILTER_TYPE_UNIVERSE];
          }
        } else {
          filters[groupType].push(value);
        }
        break;
      default:
        throw new Error('Unknown type specified.');
    }
    setFilters({ ...filters });
  };

  const preparePayloadForAlertReq = () => {
    const reqPayload = { ...payload };
    Object.keys(filters).forEach((filter_type) => {
      //severity
      if (filter_type === FILTER_TYPE_SEVERITY) {
        // if both options are selected, consider that options is unselected. else, add it to req payload
        if (filters[FILTER_TYPE_SEVERITY].length === 1) {
          reqPayload[FILTER_TYPE_SEVERITY] = filters[FILTER_TYPE_SEVERITY][0].toUpperCase();
        }
      }
      //state
      if (filter_type === FILTER_TYPE_STATE) {
        // if both options are selected, consider that options is unselected. else, add it to req payload
        if (filters[FILTER_TYPE_STATE].length === 1) {
          reqPayload['active'] = filters[FILTER_TYPE_STATE][0] === 'Active';
        }
      }
      //name
      if (filter_type === FILTER_TYPE_NAME) {
        reqPayload[FILTER_TYPE_NAME] = filters[FILTER_TYPE_NAME];
      }
      //metric name
      if (filter_type === FILTER_TYPE_METRIC_NAME) {
        reqPayload['template'] = filters[FILTER_TYPE_METRIC_NAME];
      }
      //destination
      if (filter_type === FILTER_TYPE_DESTINATION) {
        if (filters[FILTER_TYPE_DESTINATION] === NO_DESTINATION) {
          reqPayload['destinationType'] = NO_DESTINATION;
        } else if (filters[FILTER_TYPE_DESTINATION] === DEFAULT_DESTINATION) {
          reqPayload['destinationType'] = DEFAULT_DESTINATION;
        } else {
          const destination = alertDestinationList.find(
            (destination) => destination.name === filters[FILTER_TYPE_DESTINATION]
          );
          reqPayload['destinationUuid'] = destination.uuid;
          reqPayload['defaultDestination'] = destination.defaultDestination;
        }
      }
      //filter by target type
      if (filter_type === FILTER_TYPE_TARGET_TYPE) {
        if (filters[filter_type].length === 1) {
          reqPayload['targetType'] = filters[filter_type][0].toUpperCase();
          if (filters[filter_type][0] === 'Universe' && filters[FILTER_TYPE_UNIVERSE]) {
            const targetUniverse = alertUniverseList.find(
              (universe) => universe.label === filters[FILTER_TYPE_UNIVERSE]
            );
            reqPayload['target'] = {
              all: true,
              uuids: [targetUniverse.value]
            };
          }
        }
      }
    });
    return reqPayload;
  };

  useEffect(() => {
    setIsAlertListLoading(true);

    const reqPayload = preparePayloadForAlertReq();

    alertConfigs(reqPayload).then((res) => {
      // when we filter by universe uuid, we get alert associated with the universe , plus alerts associated with "ALL" universes
      // bring the alert associated only to the universe first  and "ALL" to back, to make the ux better
      if (reqPayload['target'] && reqPayload['target']['all'] === false) {
        const sortedAlerts = res.sort((a, b) => {
          if (a.target.all === b.target.all) {
            return 0;
          }
          return a.target.all === true ? 1 : -1;
        });
        bootstrapTableRef.current.cleanSort();
        setAlertList(sortedAlerts);
      } else {
        setAlertList(res);
      }

      setIsAlertListLoading(false);
    });
  }, [filters]); // eslint-disable-line react-hooks/exhaustive-deps

  // if (!getPromiseState(universes).isSuccess() && !getPromiseState(universes).isEmpty()) {
  //   return <YBLoading />;
  // }

  const clearAllFilters = () => {
    setFilters({});
  };

  return (
    <RbacValidator accessRequiredOn={ApiPermissionMap.GET_ALERT_CONFIGURATIONS}>
      <YBPanelItem
        header={header(
          isReadOnly,
          onCreateAlert,
          enablePlatformAlert,
          handleMetricsCall,
          setInitialValues,
          filterVisible,
          setFilterVisible,
          filters,
          updateFilters,
          clearAllFilters
        )}
        body={
          <Row>
            {filterVisible && (
              <Col lg={2} className="filters">
                <AlertListsWithFilter
                  metrics={metrics}
                  alertDestinationList={alertDestinationList}
                  updateFilters={updateFilters}
                  universeList={alertUniverseList}
                  alertsFilters={filters}
                />
              </Col>
            )}
            <Col lg={filterVisible ? 10 : 12} className={filterVisible && 'leftBorder'}>
              {isAlertListLoading && <YBLoading />}
              <BootstrapTable
                className="alert-list-table middle-aligned-table"
                data={alertList}
                options={{
                  ...options,
                  sizePerPage,
                  onSizePerPageList: setSizePerPage
                }}
                pagination
                condensed
                ref={bootstrapTableRef}
                maxHeight="500px"
              >
                <TableHeaderColumn dataField="uuid" isKey={true} hidden={true} />
                <TableHeaderColumn
                  dataField="name"
                  dataSort
                  columnClassName="no-border name-column"
                  dataFormat={formatName}
                  className="no-border"
                  width="20%"
                >
                  Name
                </TableHeaderColumn>
                <TableHeaderColumn
                  dataField="target"
                  dataSort
                  sortFunc={targetSortFunc}
                  columnClassName="no-border name-column"
                  className="no-border"
                  dataFormat={formatAlertTargets}
                  width={filterVisible ? '8%' : '10%'}
                >
                  Target Universes
                </TableHeaderColumn>
                <TableHeaderColumn
                  dataField="thresholds"
                  dataSort
                  sortFunc={thresholdSortFunc}
                  dataFormat={formatThresholds}
                  columnClassName="no-border name-column"
                  className="no-border"
                  width={filterVisible ? '8%' : '10%'}
                >
                  Severity
                </TableHeaderColumn>
                <TableHeaderColumn
                  dataField="destinationUUID"
                  dataSort
                  sortFunc={destinationsSortFunc}
                  dataFormat={formatRoutes}
                  columnClassName="no-border name-column"
                  className="no-border"
                  width="15%"
                >
                  Destination
                </TableHeaderColumn>
                <TableHeaderColumn
                  dataField="createTime"
                  dataSort
                  dataFormat={formatCreatedTime}
                  columnClassName="no-border name-column"
                  className="no-border"
                  width={filterVisible ? '10%' : '15%'}
                >
                  Created
                </TableHeaderColumn>
                <TableHeaderColumn
                  dataField="template"
                  dataSort
                  columnClassName="no-border name-column"
                  className="no-border"
                  width="20%"
                  dataFormat={(cell) => formatString(cell)}
                >
                  Metric Name
                </TableHeaderColumn>
                <TableHeaderColumn
                  dataField="configActions"
                  dataFormat={(cell, row, _, rowIndex) =>
                    formatConfigActions(
                      cell,
                      row,
                      rowIndex,
                      sizePerPage,
                      alertList.length,
                      options.page
                    )
                  }
                  columnClassName="yb-actions-cell"
                  className="yb-actions-cell"
                  width="5%"
                >
                  Actions
                </TableHeaderColumn>
              </BootstrapTable>
            </Col>
          </Row>
        }
        noBackground
      />
    </RbacValidator>
  );
};

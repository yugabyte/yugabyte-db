// Copyright (c) YugaByte, Inc.

import React, { useState, useEffect } from 'react';
import { usePrevious } from 'react-use';
import { Link } from 'react-router';
import { Dropdown, Tooltip, OverlayTrigger } from 'react-bootstrap';
import { useQuery } from 'react-query';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { useSelector } from 'react-redux';
import moment from 'moment';
import _, { find } from 'lodash';

import {
  getUniversePendingTask,
  getUniverseStatus,
  getUniverseStatusIcon,
  hasPendingTasksForUniverse,
  UniverseState
} from '../helpers/universeHelpers';
import {
  YBCost,
  YBFormattedNumber,
  YBLabelWithIcon,
  YBResourceCount
} from '../../common/descriptors';
import {
  getClusterProviderUUIDs,
  getProviderMetadata,
  getUniverseNodeCount,
  isPausableUniverse
} from '../../../utils/UniverseUtils';
import {
  DeleteUniverseContainer,
  ToggleUniverseStateContainer,
  YBUniverseItem
} from '../../universes';
import {
  getFeatureState,
  isNotHidden,
  isDisabled,
  showOrRedirect
} from '../../../utils/LayoutUtils';
import { api } from '../../../redesign/helpers/api';
import { isNonEmptyArray, isNonEmptyObject, isDefinedNotNull } from '../../../utils/ObjectUtils';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { QuerySearchInput } from '../../queries/QuerySearchInput';
import { filterBySearchTokens } from '../../queries/helpers/queriesHelper';
import { YBControlledSelect, YBButton, YBMultiSelectRedesiged } from '../../common/forms/fields';
import { timeFormatter } from '../../../utils/TableFormatters';
import YBPagination from '../../tables/YBPagination/YBPagination';
import { isEphemeralAwsStorageInstance } from '../UniverseDetail/UniverseDetail';
import { YBMenuItem } from '../UniverseDetail/compounds/YBMenuItem';
import ellipsisIcon from '../../common/media/more.svg';

import { YBLoadingCircleIcon } from '../../common/indicators';
import { UniverseAlertBadge } from '../YBUniverseItem/UniverseAlertBadge';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { customPermValidateFunction, RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { Action, Resource } from '../../../redesign/features/rbac';
import { getWrappedChildren } from '../../../redesign/features/rbac/common/validator/ValidatorUtils';
import './UniverseView.scss';
import 'react-bootstrap-table/css/react-bootstrap-table.css';

/**
 * The tableData key allows us to use a different field from the universe
 * for rendering the data in table view.
 **/
const dropdownFieldKeys = {
  'Universe Name': {
    value: 'name',
    type: 'string'
  },
  'Provider Type': {
    value: 'providerTypes',
    type: 'stringArray'
  },
  'Provider Name': {
    value: 'providerNames',
    type: 'stringArray'
  },
  'Price per Month': {
    value: 'pricePerMonth',
    type: 'number'
  },
  'Creation Date': {
    value: 'creationDate',
    type: 'timestamp'
  },
  Status: {
    value: 'statusText',
    tableData: 'status',
    type: 'string'
  }
};

const view = {
  TABLE: 'table',
  LIST: 'list'
};

const order = {
  // Keys set to 'asc' and 'desc' to match react-boostrap-table
  asc: {
    text: 'Ascending',
    value: 'asc'
  },
  desc: {
    text: 'Descending',
    value: 'desc'
  }
};

// tableDataValueToKey is used for reverse mapping from table data field to user search field.
const tableDataValueToKey = {
  name: 'Universe Name',
  providerTypes: 'Provider Type',
  pricePerMonth: 'Price per Month',
  creationDate: 'Creation Date',
  status: 'Status'
};

const toggleTooltip = (view) => <Tooltip id="tooltip">Switch to {view} view.</Tooltip>;

const { ...filterStatuses } = UniverseState;
const filterStatusesArr = Object.values(filterStatuses).map((status) => ({
  value: status.text,
  label: status.text
}));

const TABLE_MIN_PAGE_SIZE = 10;
const LIST_MIN_PAGE_SIZE = 4;

const ALERT_REFETCH_INTERVAL = 60000;

const defaultAlertFilters = {
  states: ['ACTIVE'],
  severities: ['SEVERE', 'WARNING'],
  configurationTypes: ['UNIVERSE']
};

export const UniverseView = (props) => {
  const [searchTokens, setSearchTokens] = useState([]);
  const [sortField, setSortField] = useState('Universe Name');
  const [sortOrder, setSortOrder] = useState(order.asc.value);
  const [universePendingTasks, setUniversePendingTasks] = useState({});
  const [pageSize, setPageSize] = useState(4);
  const [activePage, setActivePage] = useState(1);
  const [curView, setCurView] = useState(view.LIST);
  const [curStatusFilter, setCurStatusFilter] = useState([]);
  const [focusedUniverse, setFocusedUniverse] = useState();
  const runtimeConfigs = useSelector((state) => state.customer.runtimeConfigs);

  const {
    universe: { universeList },
    customer: { currentCustomer },
    tasks: { customerTaskList },
    modal: { showModal, visibleModal },
    closeModal,
    showToggleUniverseStateModal,
    showDeleteUniverseModal,
    featureFlags
  } = props;

  const universeUUIDs = universeList?.data
    ? universeList.data.map((universe) => universe.universeUUID)
    : [];
  const prevUniverseUUIDs = usePrevious(universeUUIDs);
  const prevCustomerTaskList = usePrevious(customerTaskList);

  useEffect(() => {
    if (!runtimeConfigs) {
      props.fetchGlobalRunTimeConfigs();
    }
    props.fetchUniverseMetadata();
    props.fetchUniverseTasks();
    return () => props.resetUniverseTasks();
  }, []); // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    if (
      !_.isEqual(universeUUIDs, prevUniverseUUIDs) ||
      !_.isEqual(customerTaskList, prevCustomerTaskList)
    ) {
      updateUniversePendingTasks(universeUUIDs, customerTaskList);
    }

    if (universeList && isNonEmptyArray(universeList.data)) {
      for (const universe of universeList.data) {
        if (
          universe.universeDetails.updateInProgress &&
          !hasPendingTasksForUniverse(universe.universeUUID, customerTaskList) &&
          hasPendingTasksForUniverse(universe.universeUUID, prevCustomerTaskList)
        ) {
          props.fetchUniverseMetadata();
        }
      }
    }
  }, [universeList.data, customerTaskList]); // eslint-disable-line react-hooks/exhaustive-deps

  const updateUniversePendingTasks = (universeUUIDs, customerTaskList) => {
    const newUniversePendingTasks = {};
    universeUUIDs.forEach((universeUUID) => {
      const universePendingTask = getUniversePendingTask(universeUUID, customerTaskList);
      newUniversePendingTasks[universeUUID] = universePendingTask;
    });
    setUniversePendingTasks(newUniversePendingTasks);
  };

  const handleSearchTokenChange = (newTokens) => {
    setSearchTokens(newTokens);
    setActivePage(1);
  };

  const handleSortFieldChange = (newField) => {
    setSortField(newField);
  };

  const handleSortOrderChange = (newOrder) => {
    setSortOrder(newOrder);
  };

  const handleViewChange = (newView) => {
    setCurView(newView);
  };

  const handlePageSizeChange = (newSize) => {
    setPageSize(newSize);
  };

  const handleStatusFilterChange = (value) => {
    setCurStatusFilter(value ? value : []);
    setActivePage(1);
  };

  const formatUniverseState = (status, row) => {
    const currentUniverseFailedTask = customerTaskList?.filter((task) => {
      return (
        task.targetUUID === row.universeUUID &&
        (task.status === 'Failure' || task.status === 'Aborted')
      );
    });
    const failedTask = currentUniverseFailedTask?.[0];
    return (
      <div className={`universe-status-cell ${status.className}`}>
        <div>
          {getUniverseStatusIcon(status)}
          <span>
            {status.text === 'Error' && failedTask
              ? `${failedTask.type} ${failedTask.target} failed`
              : status.text}
          </span>
        </div>
        <UniverseAlertBadge universeUUID={row.universeUUID} listView />
      </div>
    );
  };

  const formatCost = (pricePerHour, row) => {
    const isPricingKnown = row.resources?.pricingKnown;
    return (
      <div className="universe-cost-cell">
        <YBCost
          value={pricePerHour}
          multiplier="month"
          base="month"
          isPricingKnown={isPricingKnown}
          runtimeConfigs={runtimeConfigs}
        />
      </div>
    );
  };

  const formatUniverseName = (universeName, row) => {
    return <Link to={`/universes/${row.universeUUID}`}>{universeName}</Link>;
  };

  const formatUniverseActions = (_, row) => {
    const isEphemeralAwsStorage =
      row.universeDetails.nodeDetailsSet.find?.((node) => {
        return isEphemeralAwsStorageInstance(node.cloudInfo?.instance_type);
      }) !== undefined;
    const universePaused = row.universeDetails.universePaused;
    return (
      <Dropdown id="table-actions-dropdown" pullRight>
        <Dropdown.Toggle noCaret>
          <img src={ellipsisIcon} alt="more" className="ellipsis-icon" />
        </Dropdown.Toggle>
        <Dropdown.Menu>
          {isPausableUniverse(row) &&
            !isEphemeralAwsStorage &&
            (featureFlags.test['pausedUniverse'] || featureFlags.released['pausedUniverse']) && (
              <RbacValidator
                isControl
                accessRequiredOn={{
                  onResource: row.universeUUID,
                  ...ApiPermissionMap.RESUME_UNIVERSE
                }}
              >
                <YBMenuItem
                  onClick={() => {
                    setFocusedUniverse(row);
                    showToggleUniverseStateModal();
                  }}
                  availability={getFeatureState(
                    currentCustomer.data.features,
                    'universes.details.overview.pausedUniverse'
                  )}
                >
                  <YBLabelWithIcon
                    icon={universePaused ? 'fa fa-play-circle-o' : 'fa fa-pause-circle-o'}
                  >
                    {universePaused ? 'Resume Universe' : 'Pause Universe'}
                  </YBLabelWithIcon>
                </YBMenuItem>
              </RbacValidator>
            )}
          <RbacValidator
            isControl
            accessRequiredOn={{
              onResource: row.universeUUID,
              ...ApiPermissionMap.DELETE_UNIVERSE
            }}
            overrideStyle={{ display: 'block' }}
          >
            <YBMenuItem
              onClick={() => {
                setFocusedUniverse(row);
                showDeleteUniverseModal();
              }}
              availability={getFeatureState(
                currentCustomer.data.features,
                'universes.details.overview.deleteUniverse'
              )}
            >
              <YBLabelWithIcon icon="fa fa-trash-o fa-fw">Delete Universe</YBLabelWithIcon>
            </YBMenuItem>
          </RbacValidator>
        </Dropdown.Menu>
      </Dropdown>
    );
  };

  const universeSortFunction = (a, b) => {
    let ord = 0;
    if (sortField === 'Creation Date') {
      ord = Date.parse(a.creationDate) - Date.parse(b.creationDate);
    } else {
      // Break ties with universe name in ascending order
      const sortKey = dropdownFieldKeys[sortField].value;
      ord = a[sortKey] < b[sortKey] ? -1 : 1;
      if (a[sortKey] === b[sortKey]) {
        return a.name < b.name ? -1 : 1;
      }
    }

    return sortOrder === order.asc.value ? ord : ord * -1;
  };

  const getUniverseListItems = (universes) => {
    return universes
      .sort(universeSortFunction)
      .slice((activePage - 1) * pageSize, activePage * pageSize)
      .map((item, idx) => {
        return (
          // eslint-disable-next-line react/no-unknown-property
          <li className="universe-list-item" key={item.universeUUID} idx={idx}>
            <YBUniverseItem {...props} universe={item} runtimeConfigs={runtimeConfigs} />
          </li>
        );
      });
  };

  const renderView = (universes) => {
    const curSortObj = dropdownFieldKeys[sortField];
    const tableOptions = {
      sortName: Object.prototype.hasOwnProperty.call(curSortObj, 'tableData')
        ? curSortObj.tableData
        : curSortObj.value,
      sortOrder: sortOrder,
      onSortChange: (sortName, sortOrder) => {
        handleSortFieldChange(tableDataValueToKey[sortName]);
        handleSortOrderChange(sortOrder);
      }
    };

    if (curView === view.LIST) {
      if (!universeList || !_.isObject(universeList)) {
        return null;
      }
      if (getPromiseState(universeList).isInit() || getPromiseState(universeList).isLoading()) {
        return <YBLoadingCircleIcon />;
      }
      if (!isNonEmptyArray(universeList.data)) {
        return (
          <h5>
            <span>No universes defined.</span>
          </h5>
        );
      }
      if (!isNonEmptyArray(universes)) {
        return (
          <h5>
            <span>No universes matched.</span>
          </h5>
        );
      }

      return (
        <React.Fragment>
          <ul className="list-group" aria-label="Universe List">
            {getUniverseListItems(universes)}
          </ul>
          {universes.length > LIST_MIN_PAGE_SIZE && (
            <div className="list-pagination-control">
              <YBControlledSelect
                options={[4, 10, 20, 30, 40].map((option, idx) => (
                  <option key={option} id={idx} value={option}>
                    {option}
                  </option>
                ))}
                selectVal={pageSize}
                onInputChanged={(event) => handlePageSizeChange(event.target.value)}
              />
              <YBPagination
                className="universe-list-pagination"
                numPages={Math.ceil(universes.length / pageSize)}
                onChange={(newPageNum) => {
                  setActivePage(newPageNum);
                }}
                activePage={activePage}
              />
            </div>
          )}
        </React.Fragment>
      );
    }
    if (curView === view.TABLE) {
      return (
        <BootstrapTable
          data={universes}
          className="universe-table"
          trClassName="tr-row-style"
          options={tableOptions}
          pagination={universes.length > TABLE_MIN_PAGE_SIZE}
          hover
        >
          <TableHeaderColumn
            dataField="universeUUID"
            isKey={true}
            hidden={true}
            columnClassName="no-border"
          >
            Universe UUID
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="name"
            dataSort
            sortFunc={(a, b, _) => universeSortFunction(a, b)}
            columnClassName="no-border"
            dataFormat={formatUniverseName}
          >
            Universe Name
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="providerTypes"
            tdStyle={{ whiteSpace: 'normal' }}
            columnClassName="no-border"
          >
            Provider Types
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="providerNames"
            tdStyle={{ whiteSpace: 'normal' }}
            columnClassName="no-border"
          >
            Provider Names
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="creationDate"
            dataFormat={timeFormatter}
            dataSort
            sortFunc={(a, b, _) => universeSortFunction(a, b)}
            tdStyle={{ whiteSpace: 'normal' }}
            columnClassName="no-border"
          >
            Creation Date
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="pricePerMonth"
            dataFormat={formatCost}
            dataSort
            sortFunc={(a, b, _) => universeSortFunction(a, b)}
            headerAlign="right"
            tdStyle={{ whiteSpace: 'normal' }}
            thStyle={{ paddingRight: '100px' }}
            columnClassName="no-border"
          >
            Price / Month
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="status"
            dataFormat={(cell, row) => formatUniverseState(cell, row)}
            dataSort
            sortFunc={(a, b, _) => universeSortFunction(a, b)}
            tdStyle={{ whiteSpace: 'normal' }}
            columnClassName="no-border"
          >
            Status
          </TableHeaderColumn>
          <TableHeaderColumn
            dataFormat={formatUniverseActions}
            columnClassName="universe-action-column no-border"
            width="50px"
          ></TableHeaderColumn>
        </BootstrapTable>
      );
    }
  };

  showOrRedirect(currentCustomer.data.features, 'menu.universes');

  let universes =
    _.isObject(universeList) && isNonEmptyArray(universeList.data)
      ? universeList.data.map((universeBase) => {
        const universe = _.cloneDeep(universeBase);
        universe.pricePerMonth = universe.pricePerHour * 24 * moment().daysInMonth();

        const clusterProviderUUIDs = getClusterProviderUUIDs(universe.universeDetails.clusters);
        const clusterProviders = props.providers.data.filter((p) =>
          clusterProviderUUIDs.includes(p.uuid)
        );
        universe.providerTypes = clusterProviders.map((provider) => {
          return getProviderMetadata(provider).name;
        });
        universe.providerNames = clusterProviders.map((provider) => provider.name);

        const universeStatus = getUniverseStatus(
          universe,
          universePendingTasks[universe.universeUUID]
        );
        universe.status = universeStatus.state;
        universe.statusText = universeStatus.state.text;
        return universe;
      })
      : [];

  const statusFilterTokens = curStatusFilter.map((status) => ({
    key: 'statusText',
    label: 'Status',
    value: status.value
  }));
  universes = filterBySearchTokens(universes, searchTokens, dropdownFieldKeys, statusFilterTokens);

  const filteredUniverses = universes.reduce(
    (prev, curUniverse) => ({
      ...prev,
      [curUniverse.universeUUID]: curUniverse.statusText
    }),
    {}
  );
  const { data: alertCount } = useQuery(
    ['alerts', { ...defaultAlertFilters, ...filteredUniverses }],
    () =>
      api.getAlertCount({ ...defaultAlertFilters, sourceUUIDs: Object.keys(filteredUniverses) }),
    { refetchInterval: ALERT_REFETCH_INTERVAL }
  );

  let numNodes = 0;
  let numOfCores = 0;
  let totalCost = 0;
  if (universes) {
    universes.forEach(function (universeItem) {
      if (isNonEmptyObject(universeItem.universeDetails)) {
        numNodes += getUniverseNodeCount(universeItem.universeDetails.nodeDetailsSet);
        numOfCores += universeItem.resources.numCores;
      }
      if (isDefinedNotNull(universeItem.pricePerHour)) {
        totalCost += universeItem.pricePerHour * 24 * moment().daysInMonth();
      }
    });
  }


  if (!customPermValidateFunction((userPermissions) => {
    return find(userPermissions, { resourceType: Resource.UNIVERSE, actions: [Action.READ] }) !== undefined;
  })) {
    return getWrappedChildren({});
  }

  return (
    <React.Fragment>
      <DeleteUniverseContainer
        visible={showModal && visibleModal === 'deleteUniverseModal'}
        onHide={closeModal}
        title="Delete Universe: "
        body="Are you sure you want to delete the universe? You will lose all your data!"
        type="primary"
        focusedUniverse={focusedUniverse}
      />

      <ToggleUniverseStateContainer
        visible={showModal && visibleModal === 'toggleUniverseStateForm'}
        onHide={closeModal}
        title={`${focusedUniverse?.universeDetails.universePaused ? 'Resume' : 'Pause'} Universe: `}
        type="primary"
        universePaused={focusedUniverse?.universeDetails.universePaused}
        focusedUniverse={focusedUniverse}
      />
      <div className="universe-action-bar">
        <QuerySearchInput
          id="universe-list-search-bar"
          columns={dropdownFieldKeys}
          placeholder="Filter by universe details"
          searchTerms={searchTokens}
          onSubmitSearchTerms={handleSearchTokenChange}
        />
        {isNotHidden(currentCustomer.data.features, 'universe.create') && (
          <RbacValidator
            accessRequiredOn={{
              ...ApiPermissionMap.CREATE_UNIVERSE
            }}
            isControl
          >
            <Link to="/universes/create">
              <YBButton
                btnClass="universe-button btn btn-lg btn-orange"
                disabled={isDisabled(currentCustomer.data.features, 'universe.create')}
                btnText="Create Universe"
                btnIcon="fa fa-plus"
                data-testid="UniverseList-CreateUniverse"
              />
            </Link>
          </RbacValidator>
        )}
      </div>
      <div className="universes-stats-container">
        <YBResourceCount
          kind="Universe"
          pluralizeKind
          separatorLine
          icon="fa-globe"
          size={universes.length}
        />
        <YBResourceCount
          kind="Node"
          pluralizeKind
          separatorLine
          icon="fa-braille"
          size={numNodes}
        />
        <YBResourceCount
          kind="Core"
          pluralizeKind
          separatorLine
          icon="fa-microchip"
          size={numOfCores}
        />
        <YBResourceCount
          kind="per Month"
          separatorLine
          icon="fa-money"
          sizePrefix="$"
          size={<YBFormattedNumber value={totalCost} maximumFractionDigits={2} />}
        />
        <YBResourceCount
          kind="Active Alert"
          pluralizeKind
          separatorLine
          icon="fa-bell-o"
          size={isNonEmptyObject(filteredUniverses) ? alertCount : 0}
        />
      </div>
      <div className="universe-view-toolbar-container">
        <YBMultiSelectRedesiged
          className="universe-status-filter"
          name="statuses"
          placeholder=""
          options={filterStatusesArr}
          value={curStatusFilter}
          onChange={handleStatusFilterChange}
        />
        <OverlayTrigger
          placement="left"
          delayShow={1000}
          delayHide={500}
          overlay={toggleTooltip(curView === view.LIST ? view.TABLE : view.LIST)}
        >
          <YBButton
            className="view-toggle"
            btnIcon={curView === view.LIST ? 'fa fa-table' : 'fa fa-list-ul'}
            aria-label={`${curView === view.LIST ? 'Table' : 'List'} View`}
            defaultValue={curView}
            onClick={(e) => {
              handleViewChange(curView === view.LIST ? view.TABLE : view.LIST);
              e.currentTarget.blur();
            }}
          />
        </OverlayTrigger>
      </div>
      <div className="universe-view-data-container" aria-label="Universe Data">
        {renderView(universes)}
      </div>
    </React.Fragment>
  );
};

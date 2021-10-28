// Copyright (c) YugaByte, Inc.

import React, { useState, useEffect } from 'react';
import { usePrevious } from 'react-use';
import { Link } from 'react-router';
import { ListGroup, ToggleButtonGroup, ToggleButton } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import moment from 'moment';
import _ from 'lodash';

import {
  getUniversePendingTask,
  getUniverseStatus,
  getUniverseStatusIcon,
  hasPendingTasksForUniverse
} from '../helpers/universeHelpers';

import { YBResourceCount } from '../../common/descriptors';
import { getUniverseNodes } from '../../../utils/UniverseUtils';
import { YBFormattedNumber } from '../../common/descriptors';
import { isNonEmptyArray, isNonEmptyObject, isDefinedNotNull } from '../../../utils/ObjectUtils';
import { getClusterProviderUUIDs, getProviderMetadata } from '../../../utils/UniverseUtils';
import { isNotHidden, isDisabled, showOrRedirect } from '../../../utils/LayoutUtils';
import { QuerySearchInput } from '../../queries/QuerySearchInput';
import { filterBySearchTokens } from '../../queries/helpers/queriesHelper';
import { YBControlledSelect } from '../../common/forms/fields';
import { YBButton } from '../../common/forms/fields';
import { YBUniverseItem } from '../../universes';
import { YBCost } from '../../common/descriptors';
import { timeFormatter } from '../../../utils/TableFormatters';
import YBPagination from '../../tables/YBPagination/YBPagination';

import 'react-bootstrap-table/css/react-bootstrap-table.css';
import './UniverseView.scss';

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

const tableMinPageSize = 10;
const listMinPageSize = 4;

export const UniverseView = (props) => {
  const [searchTokens, setSearchTokens] = useState([]);
  const [sortField, setSortField] = useState('Universe Name');
  const [sortOrder, setSortOrder] = useState(order.asc.value);
  const [universePendingTasks, setUniversePendingTasks] = useState({});
  const [pageSize, setPageSize] = useState(4);
  const [activePage, setActivePage] = useState(1);
  const [curView, setCurView] = useState(view.LIST);

  const {
    universe: { universeList },
    customer: { currentCustomer },
    tasks: { customerTaskList }
  } = props;

  const universeUUIDs =
    universeList && universeList.data
      ? universeList.data.map((universe) => universe.universeUUID)
      : [];
  const prevUniverseUUIDs = usePrevious(universeUUIDs);
  const prevCustomerTaskList = usePrevious(customerTaskList);

  useEffect(() => {
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

    if (universeList) {
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

  const formatUniverseState = (status) => {
    return (
      <div className={`universe-status-cell ${status.statusClassName}`}>
        {getUniverseStatusIcon(status)}
        <span>{status.statusText}</span>
      </div>
    );
  };

  const formatCost = (pricePerHour) => {
    return (
      <div className="universe-cost-cell">
        <YBCost value={pricePerHour} multiplier="month" base="month" />
      </div>
    );
  };

  const formatUniverseName = (universeName, row) => {
    return <Link to={`/universes/${row.universeUUID}`}>{universeName}</Link>;
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
        return <YBUniverseItem {...props} key={item.universeUUID} idx={idx} universe={item} />;
      });
  };

  const renderView = (universes) => {
    const curSortObj = dropdownFieldKeys[sortField];
    const tableOptions = {
      sortName: curSortObj.hasOwnProperty('tableData') ? curSortObj.tableData : curSortObj.value,
      sortOrder: sortOrder,
      onSortChange: (sortName, sortOrder) => {
        handleSortFieldChange(tableDataValueToKey[sortName]);
        handleSortOrderChange(sortOrder);
      }
    };

    if (curView === view.LIST) {
      if (!(_.isObject(universeList) && isNonEmptyArray(universeList.data))) {
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
          <ListGroup>{getUniverseListItems(universes)}</ListGroup>
          {universes.length > listMinPageSize && (
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
          pagination={universes.length > tableMinPageSize}
          hover
        >
          <TableHeaderColumn
            dataField="universeUUID"
            isKey={true}
            hidden={true}
            columnClassName="no-border name-column"
          >
            Universe UUID
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="name"
            dataSort
            columnClassName="no-border name-column"
            dataFormat={formatUniverseName}
          >
            Universe Name
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="providerTypes"
            tdStyle={{ whiteSpace: 'normal' }}
            columnClassName="no-border name-column"
          >
            Provider Types
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="providerNames"
            tdStyle={{ whiteSpace: 'normal' }}
            columnClassName="no-border name-column"
          >
            Provider Names
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="creationDate"
            dataFormat={timeFormatter}
            dataSort
            tdStyle={{ whiteSpace: 'normal' }}
            columnClassName="no-border name-column"
          >
            Creation Date
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="pricePerMonth"
            dataFormat={formatCost}
            dataSort
            headerAlign="right"
            width="180"
            tdStyle={{ whiteSpace: 'normal' }}
            columnClassName="no-border name-column"
          >
            Price Per Month
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="status"
            dataFormat={formatUniverseState}
            sortFunc={(a, b, sortOrder) => {
              const order = a.statusText < b.statusText ? -1 : 1;
              return sortOrder === 'asc' ? order : order * -1;
            }}
            dataSort
            tdStyle={{ whiteSpace: 'normal' }}
            columnClassName="no-border name-column"
          >
            Status
          </TableHeaderColumn>
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
          universe.status = universeStatus.status;
          universe.statusText = universeStatus.status.statusText;
          return universe;
        })
      : [];

  universes = filterBySearchTokens(universes, searchTokens, dropdownFieldKeys);
  let numNodes = 0;
  let totalCost = 0;
  if (universes) {
    universes.forEach(function (universeItem) {
      if (isNonEmptyObject(universeItem.universeDetails)) {
        numNodes += getUniverseNodes(universeItem.universeDetails.clusters);
      }
      if (isDefinedNotNull(universeItem.pricePerHour)) {
        totalCost += universeItem.pricePerHour * 24 * moment().daysInMonth();
      }
    });
  }
  return (
    <React.Fragment>
      <div className="universe-action-bar">
        <QuerySearchInput
          id="universe-list-search-bar"
          columns={dropdownFieldKeys}
          placeholder="Filter by universe details"
          searchTerms={searchTokens}
          onSubmitSearchTerms={handleSearchTokenChange}
        />
        {isNotHidden(currentCustomer.data.features, 'universe.import') && (
          <Link to="/universes/import">
            <YBButton
              btnClass="universe-button btn btn-lg btn-default"
              disabled={isDisabled(currentCustomer.data.features, 'universe.import')}
              btnText="Import Universe"
              btnIcon="fa fa-mail-forward"
            />
          </Link>
        )}
        {isNotHidden(currentCustomer.data.features, 'universe.create') && (
          <Link to="/universes/create">
            <YBButton
              btnClass="universe-button btn btn-lg btn-orange"
              disabled={isDisabled(currentCustomer.data.features, 'universe.create')}
              btnText="Create Universe"
              btnIcon="fa fa-plus"
            />
          </Link>
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
          kind="per Month"
          separatorLine
          icon="fa-money"
          sizePrefix="$"
          size={<YBFormattedNumber value={totalCost} maximumFractionDigits={2} />}
        />
        {/* TODO: Support fetching and filtering alert by a group of universes */}
        {/* <YBResourceCount kind="Alert" pluralizeKind separatorLine icon="fa-bell-o" size={0} /> */}
      </div>
      <div className="universe-view-toolbar-container">
        <ToggleButtonGroup
          type="radio"
          name="options"
          className="view-toggle-button-group"
          defaultValue={curView}
          onChange={handleViewChange}
        >
          <ToggleButton id="tbg-btn-list" value={view.LIST} aria-label="List View">
            <i className="fa fa-list" aria-hidden="true" />
          </ToggleButton>
          <ToggleButton id="tbg-btn-table" value={view.TABLE} aria-label="Table View">
            <i className="fa fa-table" aria-hidden="true" />
          </ToggleButton>
        </ToggleButtonGroup>
      </div>
      <div className="universe-view-data-container">{renderView(universes)}</div>
    </React.Fragment>
  );
};

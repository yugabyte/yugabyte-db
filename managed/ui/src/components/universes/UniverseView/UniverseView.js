// Copyright (c) YugaByte, Inc.

import React, { useState, useEffect } from 'react';
import { usePrevious } from 'react-use';
import { ListGroup } from 'react-bootstrap';
import moment from 'moment';
import _ from 'lodash';

import { isNonEmptyArray } from '../../../utils/ObjectUtils';
import { getClusterProviderUUIDs, getProviderMetadata } from '../../../utils/UniverseUtils';
import { showOrRedirect } from '../../../utils/LayoutUtils';
import { QuerySearchInput } from '../../queries/QuerySearchInput';
import { filterBySearchTokens } from '../../queries/helpers/queriesHelper';
import { getUniversePendingTask, getUniverseStatus } from '../helpers/universeHelpers';
import { YBControlledSelectWithLabel } from '../../common/forms/fields';
import { YBUniverseItem } from '..';

import 'react-bootstrap-table/css/react-bootstrap-table.css';
import './UniverseView.scss';

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
    value: 'status',
    type: 'string'
  }
};

export const UniverseView = (props) => {
  const [searchTokens, setSearchTokens] = useState([]);
  const [sortField, setSortField] = useState('Universe Name');
  const [sortOrder, setSortOrder] = useState('Ascending');
  const [universePendingTasks, setUniversePendingTasks] = useState({});
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

  const universeSortFunction = (a, b) => {
    let order = 0;
    if (sortField === 'Creation Date') {
      order = Date.parse(a.creationDate) - Date.parse(b.creationDate);
    } else {
      // Break ties with universe name in ascending order
      const sortKey = dropdownFieldKeys[sortField].value;
      order = a[sortKey] < b[sortKey] ? -1 : 1;
      if (a[sortKey] === b[sortKey]) {
        return a.name < b.name ? -1 : 1;
      }
    }

    return sortOrder === 'Ascending' ? order : order * -1;
  };

  showOrRedirect(currentCustomer.data.features, 'menu.universes');

  if (!_.isObject(universeList) || !isNonEmptyArray(universeList.data)) {
    return <h5>No universes defined.</h5>;
  }
  let universes = universeList.data.map((universeBase) => {
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

    const universeStatus = getUniverseStatus(universe, universePendingTasks[universe.universeUUID]);
    universe.status = universeStatus.statusText;
    return universe;
  });

  universes = filterBySearchTokens(universes, searchTokens, dropdownFieldKeys);
  const universeRowItem = universes.sort(universeSortFunction).map((item, idx) => {
    return <YBUniverseItem {...props} key={item.universeUUID} idx={idx} universe={item} />;
  });

  return (
    <React.Fragment>
      <div className="universe-list-table-toolbar-container">
        <YBControlledSelectWithLabel
          label="Sort Field"
          options={Object.keys(dropdownFieldKeys)
            .filter((field) => dropdownFieldKeys[field].type !== 'stringArray')
            .map((field, idx) => (
              <option key={idx} id={idx}>
                {field}
              </option>
            ))}
          selectVal={sortField}
          onInputChanged={(event) => handleSortFieldChange(event.target.value)}
        />
        <YBControlledSelectWithLabel
          label="Sort Order"
          options={['Ascending', 'Descending'].map((field, idx) => (
            <option key={idx} id={idx}>
              {field}
            </option>
          ))}
          selectVal={sortOrder}
          onInputChanged={(event) => handleSortOrderChange(event.target.value)}
        />
        <div className="universe-list-table-toolbar-button-container">{props.children}</div>
      </div>
      <QuerySearchInput
        id="universe-list-search-bar"
        columns={dropdownFieldKeys}
        placeholder="Filter by universe details"
        searchTerms={searchTokens}
        onSubmitSearchTerms={handleSearchTokenChange}
      />
      <ListGroup>{universeRowItem}</ListGroup>
    </React.Fragment>
  );
};

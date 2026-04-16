import React, { FC, useMemo, useState } from 'react';
import { Row } from 'react-bootstrap';
import { YBCheckBox } from '../../common/forms/fields';
import { lowerCase, startCase } from 'lodash';
import Select, { OptionsType } from 'react-select';

import './AlertsFilter.scss';
import { YBSearchInput } from '../../common/forms/fields/YBSearchInput';

export const FILTER_TYPE_NAME = 'name';
export const FILTER_TYPE_SEVERITY = 'severity';
export const FILTER_TYPE_TARGET_TYPE = 'targetType';
export const FILTER_TYPE_UNIVERSE = 'universe';
export const FILTER_TYPE_METRIC_NAME = 'metricName';
export const FILTER_TYPE_DESTINATION = 'destinations';
export const FILTER_TYPE_STATE = 'state';

export const NO_DESTINATION = 'NO_DESTINATION';
export const NO_DESTINATION_OPTION = [{ name: NO_DESTINATION }];

export const DEFAULT_DESTINATION = 'DEFAULT_DESTINATION';
export const DEFAULT_DESTINATION_OPTION = [{ name: DEFAULT_DESTINATION }];

const TARGET_TYPE = ['Platform', 'Universe'];
const STATE = ['Active', 'Inactive'];
const SEVERITY = ['Severe', 'Warning'];

export const formatString = (text:string) => startCase(lowerCase(text));

interface MetricsSchema {
  template: string;
  [key: string]: any;
}

interface AlertDestinationSchema {
  uuid: string;
  customerUUID: string;
  name: string;
  defaultDestination: boolean;
  [key: string]: any;
}

type AlertsFiltersSchema = { [key: string]: any };
type UpdateFiltersFunction = (
  groupType: string,
  value: string,
  filterType: string,
  remove?: boolean
) => void;
interface AlertsFilterProps {
  metrics: MetricsSchema[];
  alertDestinationList: AlertDestinationSchema[];
  universeList: { label: string; value: string }[];
  updateFilters: UpdateFiltersFunction;
  alertsFilters: AlertsFiltersSchema;
  clearAllFilters: Function;
}

/**
 * Generate checkboxes
 * @param title title of the filter/field
 * @param filterType type of filter available, eg, universe, metricName, destinations, etc
 * @param sourceArray options available for the filter type
 * @param updateFilters function to call , when the checkbox value is changed
 * @param alertsFilters current applied filters
 * @returns Checkbox elements
 */
const getCheckboxControl = (
  title: string,
  filterType: string,
  sourceArray: string[],
  updateFilters: UpdateFiltersFunction,
  alertsFilters: AlertsFiltersSchema
) => {
  return (
    <Row className="field">
      <div className="field-label">{title}</div>
      {sourceArray.map((fieldName) => {
        return (
          <div key={fieldName}>
            <YBCheckBox
              label={<span className="checkbox-label">{fieldName}</span>}
              input={{
                checked: alertsFilters[filterType]?.includes(fieldName)
              }}
              field={{
                onChange: () => updateFilters(filterType, fieldName, 'multiselect')
              }}
            />
          </div>
        );
      })}
    </Row>
  );
};

/**
 * Generates select filter type dom
 * @param title title for the  filter
 * @param filtersMap map that contains all current applied filters
 * @param filterType type of the filter like state, severity etc
 * @param selectOptions values for the select control
 * @param updateFilters function to call , when the checkbox value is changed
 * @returns Select control Element
 */
const getSelectControl = (
  title: string,
  filtersMap: AlertsFiltersSchema,
  filterType: string,
  selectOptions: OptionsType<{ label: string; value: string }>,
  updateFilters: UpdateFiltersFunction
) => {
  //Only format for metric names
  const label = filtersMap[filterType] ? (filterType === FILTER_TYPE_METRIC_NAME ? formatString(filtersMap[filterType]): filtersMap[filterType]) : null;
  return (
    <Row className="field">
      <div className="field-label">{title}</div>
      <Select
        value={
          filtersMap[filterType]
            ? {
              value: filtersMap[filterType],
              label 
            }
            : null
        }
        isMulti={false}
        isClearable
        options={selectOptions}
        onChange={(val: any, action) =>
          updateFilters(filterType, action.action === 'clear' ? '' : val.value, 'text')
        }
        menuPortalTarget={document.body}
      />
    </Row>
  );
};

/**
 * Generates Filters Dom
 * @param metrics metrics available for alerts
 * @param alertDestinationList destinations available
 * @param universeList universes available
 * @param updateFilters callback fn, called when the filter is modified
 * @param alertsFilters current applied filter values
 * @returns
 */
export const AlertListsWithFilter: FC<AlertsFilterProps> = ({
  metrics,
  alertDestinationList,
  universeList,
  updateFilters,
  alertsFilters
}) => {
  const metricNames = useMemo(
    () =>
      Array.from(new Set(metrics?.map((metric) => metric.template))).map((template) => {
        return { label: formatString(template), value: template };
      }),
    [metrics]
  );

  const destinations = useMemo(
    () =>
      Array.from(
        new Set(
          [...NO_DESTINATION_OPTION, ...DEFAULT_DESTINATION_OPTION, ...alertDestinationList ?? []].map(
            (destination) => destination.name
          )
        )
      ).map((destination) => {
        return { label: formatString(destination), value: destination };
      }),
    [alertDestinationList]
  );

  const universeLists = useMemo(
    () =>
      universeList.map((universe) => {
        return { value: universe.label, label: universe.label };
      }),
    [universeList]
  );

  const [searchText, setSearchText] = useState(alertsFilters[FILTER_TYPE_NAME]);

  return (
    <div className="filter-panel">
      <Row className="field">
        <div className="field-label">Name</div>
        <YBSearchInput
          placeHolder="Search..."
          val={searchText}
          onValueChanged={(e: React.ChangeEvent<HTMLInputElement>) => {
            setSearchText(e.target.value);
          }}
          onEnterPressed={() => updateFilters(FILTER_TYPE_NAME, searchText, 'text')}
        />
      </Row>

      {getCheckboxControl(
        'Type',
        FILTER_TYPE_TARGET_TYPE,
        TARGET_TYPE,
        updateFilters,
        alertsFilters
      )}

      {alertsFilters[FILTER_TYPE_TARGET_TYPE]?.includes('Universe') &&
        getSelectControl(
          'Universe',
          alertsFilters,
          FILTER_TYPE_UNIVERSE,
          universeLists,
          updateFilters
        )}

      {getSelectControl(
        'Metric Name',
        alertsFilters,
        FILTER_TYPE_METRIC_NAME,
        metricNames,
        updateFilters
      )}
      {getSelectControl(
        'Destination',
        alertsFilters,
        FILTER_TYPE_DESTINATION,
        destinations,
        updateFilters
      )}

      {getCheckboxControl('State', FILTER_TYPE_STATE, STATE, updateFilters, alertsFilters)}
      {getCheckboxControl('Severity', FILTER_TYPE_SEVERITY, SEVERITY, updateFilters, alertsFilters)}
    </div>
  );
};

/**
 * Generate pill element for filter
 * @param text text to show
 * @param key unique
 * @param removeFilter callback fn, called when 'remove' icon is clicked
 * @param icon custom icon to show
 * @returns pill JSX element
 */
const generatePills = (text: string, key: string, removeFilter: Function, icon?: string) => {
  return (
    <span key={key} className="pill">
      {icon && <i className={`pill-icon fa fa-${icon}`} />}
      {formatString(text)}
      <span className="remove-icon" onClick={() => removeFilter()}>
        X
      </span>
    </span>
  );
};

/**
 * Generate Pills from current applied filters
 * @param alertsFilters current applied filter values
 * @param updateFilters callback fn, called when the filter is modified
 * @param clearAllFilters callback fn, called when the "clear all" is clicked
 * @returns returns Pills Component
 */
export const AlertListAsPill: FC<AlertsFilterProps> = ({
  alertsFilters,
  updateFilters,
  clearAllFilters
}) => {
  return (
    <div className="pillHolder">
      {Object.keys(alertsFilters).map((filter_type) => {
        const values = alertsFilters[filter_type];

        if (filter_type === FILTER_TYPE_NAME) {
          const removeFn = () => updateFilters(filter_type, values, 'text', true);
          return generatePills(values, `${filter_type}_${values}`, removeFn, 'search');
        }

        if (Array.isArray(values)) {
          return values.map((val) => {
            const removeFn = () => updateFilters(filter_type, val, 'multiselect', true);
            return generatePills(val, `${filter_type}_${val}`, removeFn);
          });
        }

        const removeFn = () => updateFilters(filter_type, values, 'text', true);
        return generatePills(values, `${filter_type}_${values}`, removeFn);
      })}

      <span className="clear-all-filters" onClick={() => clearAllFilters()}>
        Clear All
      </span>
    </div>
  );
};

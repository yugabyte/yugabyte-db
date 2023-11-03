import { useRef, useState } from 'react';
import { YBCheckBox } from '../../../common/forms/fields';
import { Alert, DropdownButton, MenuItem } from 'react-bootstrap';
import { CustomDateRangePicker } from '../DateRangePicker/DateRangePicker';
import { useSelector } from 'react-redux';
import { find } from 'lodash';
import { convertToISODateString } from '../../../../redesign/helpers/DateUtils';
import { UniverseState } from '../../helpers/universeHelpers';

const filterTypes = [
  { label: 'Last 24 hrs', type: 'days', value: '1' },
  { label: 'Last 3 days', type: 'days', value: '3' },
  { label: 'Last 7 days', type: 'days', value: '7' },
  { type: 'divider' },
  { label: 'Custom', type: 'custom', value: 'custom' }
];
export const selectionOptions = [
  { label: 'All', value: 'All' },
  { label: 'Application logs', value: 'ApplicationLogs' },
  { label: 'Universe logs', value: 'UniverseLogs' },
  { label: 'Output files', value: 'OutputFiles' },
  { label: 'Error files', value: 'ErrorFiles' },
  { label: 'G-Flag configurations', value: 'GFlags' },
  { label: 'Instance files', value: 'Instance' },
  { label: 'Consensus meta files', value: 'ConsensusMeta' },
  { label: 'Tablet meta files', value: 'TabletMeta' },
  { label: 'Node agent logs', value: 'NodeAgent' }
];

const YbcLogsOption = { label: 'YB-Controller logs', value: 'YbcLogs' };
const K8sLogsOption = { label: 'Kubernetes Info', value: 'K8sInfo' };

const getBackDateByDay = (day) => {
  return new Date(new Date().setDate(new Date().getDate() - day));
};

export const updateOptions = (
  dateType,
  selectionOptionsValue,
  setIsDateTypeCustom,
  startDate = new Date(),
  endDate = new Date()
) => {
  if (dateType === 'custom') {
    setIsDateTypeCustom(true);
    return;
  }

  if (dateType !== 'customWithValue' && dateType !== 'custom') {
    startDate = getBackDateByDay(+dateType);
    setIsDateTypeCustom(false);
  }

  const components = [];
  selectionOptionsValue.forEach((selectionOption, index) => {
    if (index !== 0 && selectionOption) {
      components.push(selectionOptions[index].value);
    }
  });
  return {
    startDate: convertToISODateString(startDate),
    endDate: convertToISODateString(endDate),
    components: components
  };
};

export const SecondStep = ({ onOptionsChange, isK8sUniverse, universeStatus }) => {
  const [selectedFilterType, setSelectedFilterType] = useState(filterTypes[0].value);
  const [selectionOptionsValue, setSelectionOptionsValue] = useState(
    selectionOptions.map(() => true)
  );
  const [isDateTypeCustom, setIsDateTypeCustom] = useState(false);
  const refs = useRef([]);

  const featureFlags = useSelector((state) => state.featureFlags);

  if (
    (featureFlags.test.enableYbc || featureFlags.released.enableYbc) &&
    !find(selectionOptions, YbcLogsOption)
  ) {
    selectionOptions.push(YbcLogsOption);
    //check option by default
    selectionOptionsValue.push(true);
    const changedOptions = updateOptions(
      selectedFilterType,
      selectionOptionsValue,
      setIsDateTypeCustom
    );
    onOptionsChange(changedOptions);
  }

  if (isK8sUniverse && !find(selectionOptions, K8sLogsOption)) {
    selectionOptions.push(K8sLogsOption);
    selectionOptionsValue.push(true);
    const changedOptions = updateOptions(
      selectedFilterType,
      selectionOptionsValue,
      setIsDateTypeCustom
    );
    onOptionsChange(changedOptions);
  }

  return (
    <div className="universe-support-bundle-step-two">
      {universeStatus?.state !== UniverseState.GOOD && (
        <Alert bsStyle="warning">
          Support bundle creation may fail since universe is not in &ldquo;Ready&rdquo; state
        </Alert>
      )}

      <p className="subtitle-text">
        Support bundles contain the diagnostic information. This can include log files, config
        files, metadata and etc. You can analyze this information locally on your machine or send
        the bundle to Yugabyte Support team.
      </p>
      <div className="filters">
        {isDateTypeCustom && (
          <CustomDateRangePicker
            onRangeChange={(startEnd) => {
              const changedOptions = updateOptions(
                'customWithValue',
                selectionOptionsValue,
                setIsDateTypeCustom,
                startEnd.start,
                startEnd.end
              );
              onOptionsChange(changedOptions);
            }}
          />
        )}
        <DropdownButton
          title={
            <span className="dropdown-text">
              <i className="fa fa-calendar" />{' '}
              {filterTypes.find((type) => type.value === selectedFilterType).label}
            </span>
          }
          pullRight
        >
          {filterTypes.map((filterType, index) => {
            if (filterType.type === 'divider') {
              return <MenuItem divider key={filterType.type} />;
            }
            return (
              <MenuItem
                key={filterType.label}
                onClick={() => {
                  setSelectedFilterType(filterType.value);
                  const changedOptions = updateOptions(
                    filterType.value,
                    selectionOptionsValue,
                    setIsDateTypeCustom
                  );
                  onOptionsChange(changedOptions);
                }}
                value={filterType.value}
              >
                {filterType.label}
              </MenuItem>
            );
          })}
        </DropdownButton>
      </div>
      <div className="selection-area">
        <span className="title">Select what you want to include in the support bundle</span>
        {selectionOptions.map((selectionOption, index) => (
          // eslint-disable-next-line react/jsx-key
          <div className="selection-option">
            <YBCheckBox
              // eslint-disable-next-line react/no-array-index-key
              key={`${selectionOptionsValue[index]}${index}selectionOption`}
              onClick={() => {
                if (index === 0) {
                  for (
                    let internalIndex = 1;
                    internalIndex < selectionOptions.length;
                    internalIndex++
                  ) {
                    selectionOptionsValue[internalIndex] = !selectionOptionsValue[index];
                    refs.current[internalIndex].checked = !selectionOptionsValue[index];
                  }
                  selectionOptionsValue[index] = !selectionOptionsValue[index];
                  refs.current[index].checked = selectionOptionsValue[index];
                } else {
                  selectionOptionsValue[index] = !selectionOptionsValue[index];
                  refs.current[index].checked = selectionOptionsValue[index];
                  let isAllSelected = true;
                  for (
                    let internalIndex = 1;
                    internalIndex < selectionOptions.length;
                    internalIndex++
                  ) {
                    if (!selectionOptionsValue[internalIndex]) {
                      isAllSelected = false;
                    }
                  }
                  refs.current[0].checked = isAllSelected;
                }
                setSelectionOptionsValue([...selectionOptionsValue]);
                const changedOptions = updateOptions(
                  selectedFilterType,
                  [...selectionOptionsValue],
                  setIsDateTypeCustom
                );
                onOptionsChange(changedOptions);
              }}
              checkState={selectionOptionsValue[index]}
              input={{ ref: (ref) => (refs.current[index] = ref) }}
              label={selectionOption.label}
            />
          </div>
        ))}
      </div>
    </div>
  );
};

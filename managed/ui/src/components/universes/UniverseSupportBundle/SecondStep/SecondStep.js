import { useRef, useState } from 'react';
import { find } from 'lodash';
import { useQuery } from 'react-query';
import { useSelector } from 'react-redux';
import { useEffectOnce } from 'react-use';
import { Box, Typography } from '@material-ui/core';
import { Alert, DropdownButton, MenuItem } from 'react-bootstrap';
import { YBLoading } from '../../../common/indicators';
import { YBCheckBox } from '../../../common/forms/fields';
import { YBInput, YBLabel } from '../../../../redesign/components';
import { CustomDateRangePicker } from '../DateRangePicker/DateRangePicker';
import { convertToISODateString } from '../../../../redesign/helpers/DateUtils';
import { fetchGlobalRunTimeConfigs } from '../../../../api/admin';
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
  { label: 'YBA Metadata', value: 'YbaMetadata' },
  { label: 'Universe logs', value: 'UniverseLogs' },
  { label: 'Output files', value: 'OutputFiles' },
  { label: 'Error files', value: 'ErrorFiles' },
  { label: 'G-Flag configurations', value: 'GFlags' },
  { label: 'Instance files', value: 'Instance' },
  { label: 'Consensus meta files', value: 'ConsensusMeta' },
  { label: 'Tablet meta files', value: 'TabletMeta' },
  { label: 'Node agent logs', value: 'NodeAgent' }
];

const coreFileOption = { label: 'Core Files', value: 'CoreFiles' };
const YbcLogsOption = { label: 'YB-Controller logs', value: 'YbcLogs' };
const K8sLogsOption = { label: 'Kubernetes Info', value: 'K8sInfo' };

const ONE_GB_IN_BYTES = 1_07_37_41_824;

const CoreFilesProps = {
  maxNumRecentCores: 1,
  maxCoreFileSize: 25 * ONE_GB_IN_BYTES
};

const getBackDateByDay = (day) => {
  return new Date(new Date().setDate(new Date().getDate() - day));
};

export const updateOptions = (
  dateType,
  selectionOptionsValue,
  setIsDateTypeCustom,
  coreFileParams,
  startDate = new Date(),
  endDate = new Date()
) => {
  let payloadObj = {};
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
  payloadObj = {
    startDate: convertToISODateString(startDate),
    endDate: convertToISODateString(endDate),
    components: components
  };
  if (components.find((c) => c === coreFileOption.value)) {
    payloadObj = { ...payloadObj, ...coreFileParams };
  }
  return payloadObj;
};

export const SecondStep = ({ onOptionsChange, isK8sUniverse, universeStatus }) => {
  const [selectedFilterType, setSelectedFilterType] = useState(filterTypes[0].value);
  const [selectionOptionsValue, setSelectionOptionsValue] = useState(
    selectionOptions.map(() => true)
  );
  const [coreFileParams, setCoreFileParams] = useState(CoreFilesProps);
  const [isDateTypeCustom, setIsDateTypeCustom] = useState(false);
  const refs = useRef([]);
  const featureFlags = useSelector((state) => state.featureFlags);
  const { data: globalRuntimeConfigs, isLoading } = useQuery(['globalRuntimeConfigs'], () =>
    fetchGlobalRunTimeConfigs(true).then((res) => res.data)
  );

  useEffectOnce(() => {
    //This is to just check if selectiedOptions is intact with payload in universe Support bundle file
    const changedOptions = updateOptions(
      selectedFilterType,
      selectionOptionsValue,
      setIsDateTypeCustom,
      coreFileParams
    );
    onOptionsChange(changedOptions);
  });

  if (isLoading)
    return (
      <Box height="620px" display="flex" alignItems={'center'} justifyContent={'center'}>
        <YBLoading />
      </Box>
    );
  const isCoreFileEnabled =
    globalRuntimeConfigs?.configEntries?.find(
      (c) => c.key === 'yb.support_bundle.allow_cores_collection'
    )?.value === 'true';

  if (isCoreFileEnabled && !find(selectionOptions, coreFileOption)) {
    selectionOptions.push(coreFileOption);
    selectionOptionsValue.push(true);
    const changedOptions = updateOptions(
      selectedFilterType,
      selectionOptionsValue,
      setIsDateTypeCustom,
      coreFileParams
    );
    onOptionsChange(changedOptions);
  }

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
      setIsDateTypeCustom,
      coreFileParams
    );
    onOptionsChange(changedOptions);
  }

  if (isK8sUniverse && !find(selectionOptions, K8sLogsOption)) {
    selectionOptions.push(K8sLogsOption);
    selectionOptionsValue.push(true);
    const changedOptions = updateOptions(
      selectedFilterType,
      selectionOptionsValue,
      setIsDateTypeCustom,
      coreFileParams
    );
    onOptionsChange(changedOptions);
  }

  const isCoreFileSelected =
    selectionOptionsValue[selectionOptions.findIndex((e) => e.value === coreFileOption.value)];

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
                coreFileParams,
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
                    setIsDateTypeCustom,
                    coreFileParams
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
                  setIsDateTypeCustom,
                  coreFileParams
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
      {isCoreFileSelected && (
        <Box className="core-file-container">
          <Typography className="title">Override Core files properties (Optional)</Typography>
          <Box display="flex" mt={1} flexDirection={'row'} width="100%">
            <YBLabel width="225px">Maximum number of recent core files</YBLabel>
            <YBInput
              onChange={(e) => {
                const updatedObj = {
                  maxCoreFileSize: coreFileParams.maxCoreFileSize,
                  maxNumRecentCores: e.target.value < 1 ? 1 : e.target.value
                };
                setCoreFileParams({ ...updatedObj });
                const changedOptions = updateOptions(
                  selectedFilterType,
                  [...selectionOptionsValue],
                  setIsDateTypeCustom,
                  { ...updatedObj }
                );
                onOptionsChange(changedOptions);
              }}
              value={coreFileParams.maxNumRecentCores}
              type="number"
              style={{ width: '250px' }}
            />
          </Box>
          <Box display="flex" mt={1} flexDirection={'row'} width="100%">
            <YBLabel width="225px">Maximum core file size (in bytes)</YBLabel>
            <YBInput
              onChange={(e) => {
                const updatedObj = {
                  maxCoreFileSize:
                    e.target.value < 1 ? 1 * ONE_GB_IN_BYTES : e.target.value * ONE_GB_IN_BYTES,
                  maxNumRecentCores: coreFileParams.maxNumRecentCores
                };
                setCoreFileParams({ ...updatedObj });
                const changedOptions = updateOptions(
                  selectedFilterType,
                  [...selectionOptionsValue],
                  setIsDateTypeCustom,
                  { ...updatedObj }
                );
                onOptionsChange(changedOptions);
              }}
              value={coreFileParams.maxCoreFileSize / ONE_GB_IN_BYTES}
              type="number"
              style={{ width: '250px' }}
            />
          </Box>
        </Box>
      )}
    </div>
  );
};

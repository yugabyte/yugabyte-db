import { Fragment, FC } from 'react';
import { Dropdown, MenuItem } from 'react-bootstrap';
import { MetricConsts } from '../../metrics/constants';
import { isNonEmptyObject } from '../../../utils/ObjectUtils';
import { Region } from '@app/redesign/helpers/dtos';

interface RegionSelectorProps {
  onRegionChanged: (displayName: string, regionCode?: string | null, clusterUUID?: string) => void;
  currentSelectedRegion: string | null;
  selectedRegionClusterUUID: string | null;
  selectedUniverse: any | null;
  filterRegionsByUniverse?: boolean;
  selectedProvider?: any | null;
}

export interface RegionOption {
  key: string;
  label: string;
  regionCode: string | null;
  clusterUUID: string;
  isCluster: boolean;
  startsGroup: boolean;
}

export const getUniverseRegionOptions = (
  selectedUniverse: any,
  selectedProvider?: any
): { options: RegionOption[]; isDisabled: boolean } => {
  if (
    !isNonEmptyObject(selectedUniverse) ||
    selectedUniverse === MetricConsts.ALL ||
    !selectedUniverse.universeDetails.nodeDetailsSet
  ) {
    return { options: [], isDisabled: selectedUniverse === MetricConsts.ALL };
  }
  const universeClusters = selectedUniverse.universeDetails.clusters;
  const universeClustersLength = universeClusters?.length;

  // We do not need to display dropdown when there is only one cluster with one region
  if (universeClustersLength === 1 && universeClusters[0].regions.length === 1) {
    return { options: [], isDisabled: true };
  }

  const options: RegionOption[] = [];
  universeClusters?.forEach((cluster: any, clusterIdx: number) => {
    // Universe in YBA cannot have more than 1 Read Replica cluster
    const clusterDisplayName =
      cluster.clusterType === MetricConsts.PRIMARY ? 'Primary Cluster' : `Read Replica Cluster`;
    // Display cluster name only when there are multiple clusters
    if (universeClustersLength > 1) {
      options.push({
        key: `${cluster.clusterType}-${clusterIdx}`,
        label: clusterDisplayName,
        regionCode: null,
        clusterUUID: cluster.uuid,
        isCluster: true,
        startsGroup: clusterIdx > 0
      });
    }
    cluster.placementInfo?.cloudList?.[0]?.regionList?.forEach((region: any, regionIdx: number) => {
      let regionName = region?.name;
      if (!regionName) {
        regionName =
          selectedProvider?.regions?.find((r: Region) => r.uuid === region.uuid)?.name ??
          region.code;
      }
      const matches = regionName?.match(/\((.*?)\)/);
      // Return display name based on region name and code
      const regionDisplayName = matches
        ? `${matches?.[1]} (${region.code})`
        : `${region.name} (${region.code})`;
      options.push({
        key: `${clusterIdx}-region-${regionIdx}`,
        label: regionDisplayName,
        regionCode: region.code,
        clusterUUID: cluster.uuid,
        isCluster: false,
        startsGroup: false
      });
    });
  });
  return { options, isDisabled: false };
};

export const RegionSelector: FC<RegionSelectorProps> = ({
  selectedUniverse,
  onRegionChanged,
  currentSelectedRegion,
  selectedRegionClusterUUID,
  filterRegionsByUniverse = true,
  selectedProvider
}) => {
  let regionItems = [];
  let isDisabled = filterRegionsByUniverse
    ? selectedUniverse === MetricConsts.ALL
    : selectedProvider === MetricConsts.ALL;

  if (filterRegionsByUniverse) {
    const regionOptions = getUniverseRegionOptions(selectedUniverse, selectedProvider);
    isDisabled = regionOptions.isDisabled;
    regionItems = regionOptions.options.map((option) => (
      <Fragment key={option.key}>
        {option.startsGroup && <div id="region-divider" className="divider" />}
        <MenuItem
          onSelect={() => onRegionChanged(option.label, option.regionCode, option.clusterUUID)}
          // Added this line due to the issue that dropdown does not close
          // when a menu item is selected
          onClick={() => {
            document.body.click();
          }}
          eventKey={option.key}
          active={
            currentSelectedRegion === option.label &&
            option.clusterUUID === selectedRegionClusterUUID
          }
        >
          <span className={option.isCluster ? 'cluster-az-name' : 'region-name'}>
            {option.label}
          </span>
        </MenuItem>
      </Fragment>
    ));
  } else {
    // eslint-disable-next-line no-lonely-if
    if (isNonEmptyObject(selectedProvider) && selectedProvider !== MetricConsts.ALL) {
      const numProviderNodes = selectedProvider?.length;
      if (numProviderNodes === 1) {
        regionItems = [];
        isDisabled = true;
      } else if (numProviderNodes >= 1) {
        const uniqueProviderDetails = selectedProvider.filter(
          (provider: any, index: number) =>
            selectedProvider.findIndex(
              (providerItem: any) => providerItem.details.region === provider.details.region
            ) === index
        );

        regionItems = uniqueProviderDetails.map((provider: any) => {
          const providerDetails = provider.details;
          const providerRegion = providerDetails?.region;
          const key = `region-${providerDetails.region}`;
          return (
            <MenuItem
              onSelect={() => onRegionChanged(providerRegion)}
              key={key}
              // Added this line due to the issue that dropdown does not close
              // when a menu item is selected
              onClick={() => {
                document.body.click();
              }}
              eventKey={key}
              active={currentSelectedRegion === providerRegion}
            >
              <span className="region-name">{providerRegion}</span>
            </MenuItem>
          );
        });
      }
    }
  }

  // By default we need to have 'All regions' populated
  const defaultMenuItem = (
    <MenuItem
      onSelect={() => onRegionChanged(MetricConsts.ALL)}
      key={MetricConsts.ALL}
      active={currentSelectedRegion === MetricConsts.ALL}
      eventKey={MetricConsts.ALL}
    >
      {'All clusters & regions'}
    </MenuItem>
  );
  regionItems.splice(0, 0, defaultMenuItem);

  return (
    <div className="region-picker-container pull-left">
      <Dropdown
        id="regionFilterDropdown"
        className="region-filter-dropdown"
        disabled={isDisabled}
        title={
          isDisabled ? 'Select a specific universe with more than single cluster or region' : ''
        }
      >
        <Dropdown.Toggle className="dropdown-toggle-button">
          <span className="default-value">
            {currentSelectedRegion === MetricConsts.ALL ? regionItems[0] : currentSelectedRegion}
          </span>
        </Dropdown.Toggle>
        <Dropdown.Menu>
          {regionItems.length > 1 ? <div id="all-divider" className="divider" /> : null}
          {regionItems.length > 1 && regionItems}
        </Dropdown.Menu>
      </Dropdown>
    </div>
  );
};

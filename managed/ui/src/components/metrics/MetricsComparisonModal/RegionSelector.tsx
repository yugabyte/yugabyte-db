import React, { Fragment, FC } from 'react';
import { Dropdown, MenuItem } from 'react-bootstrap';

import { MetricConsts } from '../../metrics/constants';
import { isNonEmptyObject } from '../../../utils/ObjectUtils';

interface RegionSelectorData {
  selectedUniverse: any | null;
  onRegionChanged: any;
  currentSelectedRegion: string | null;
  selectedRegionClusterUUID: string | null;
}

export const RegionSelector: FC<RegionSelectorData> = ({
  selectedUniverse,
  onRegionChanged,
  currentSelectedRegion,
  selectedRegionClusterUUID,
}) => {
  let regionItems = [];
  let isDisabled = selectedUniverse === MetricConsts.ALL;

  if (
    isNonEmptyObject(selectedUniverse) &&
    selectedUniverse !== MetricConsts.ALL &&
    selectedUniverse.universeDetails.nodeDetailsSet
  ) {
    const universeClusters = selectedUniverse.universeDetails.clusters;
    const universeClustersLength = universeClusters?.length;

    // We do not need to display dropdown when there is only one cluster with one region
    if (universeClustersLength === 1 && universeClusters[0].regions.length === 1) {
      regionItems = [];
      isDisabled = true;
    } else if (universeClustersLength >= 1) {
      regionItems = universeClusters.map((cluster: any, clusterIdx: number) => {
        // Universe in YBA cannot have more than 1 Read Replica cluster
        const clusterDisplayName = cluster.clusterType === MetricConsts.PRIMARY ?
          'Primary Cluster' : `Read Replica Cluster`;

        return cluster.regions?.map((region: any, regionIdx: number) => {
          const key = `${clusterIdx}-region-${regionIdx}`;
          const matches = region.name.match(/\((.*?)\)/);
          // Return display name based on region name and code
          const regionDisplayName = matches
            ? `${matches?.[1]} (${region.code})`
            : `${region.name} (${region.code})`;

          return (
            <Fragment>
              {clusterIdx > 0 && regionIdx === 0 &&
                <div id="region-divider" className="divider" />
              }
              {/* Display cluster name only when there are multiple clusters */}
              {universeClustersLength > 1 && regionIdx === 0 ?
                <MenuItem
                  key={`${cluster.clusterType}-${clusterIdx}`}
                  onSelect={() => onRegionChanged(clusterDisplayName, null, cluster.uuid)}
                  onClick={() => { document.body.click() }}
                  eventKey={`${cluster.clusterType}-${regionIdx}`}
                  active={cluster.uuid === selectedRegionClusterUUID && currentSelectedRegion === clusterDisplayName}
                >
                  <span className="cluster-az-name">{clusterDisplayName}</span>
                </MenuItem> : null}
              <MenuItem
                onSelect={() => onRegionChanged(regionDisplayName, region.code, cluster.uuid)}
                key={key}
                // Added this line due to the issue that dropdown does not close
                // when a menu item is selected
                onClick={() => { document.body.click() }}
                eventKey={`${region.uuid}-${regionIdx}`}
                active={currentSelectedRegion === regionDisplayName && cluster.uuid === selectedRegionClusterUUID}
              >
                <span className="region-name">{regionDisplayName}</span>
              </MenuItem>
            </Fragment>
          );
        })
      })
    }
  }

  // By default we need to have 'All regions' populated
  const defaultMenuItem = (
    <Fragment>
      <MenuItem
        onSelect={() => onRegionChanged(MetricConsts.ALL)}
        key={MetricConsts.ALL}
        active={currentSelectedRegion === MetricConsts.ALL}
        // Added this line due to the issue that dropdown does not close
        // when a menu item is selected
        onClick={() => { document.body.click() }}
        eventKey={MetricConsts.ALL}
      >
        {'All clusters & regions'}
      </MenuItem>
    </Fragment>
  );
  regionItems.splice(0, 0, defaultMenuItem);

  return (
    <div className="region-picker-container pull-left">
      <Dropdown
        id="region-filter-dropdown"
        disabled={isDisabled}
        title={isDisabled ? "Select a specific universe with more than single cluster or region" : ""}
      >
        <Dropdown.Toggle className="dropdown-toggle-button">
          <span className="default-region-value">
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

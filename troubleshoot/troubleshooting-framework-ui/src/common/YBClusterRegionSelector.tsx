import { MenuItem, makeStyles, Divider } from '@material-ui/core';
import clsx from 'clsx';
import { YBSelect } from './YBSelect';
import { isNonEmptyArray, isNonEmptyString } from '../helpers/ObjectUtils';
import { ALL_REGIONS } from '../helpers/constants';

import treeIcon from '../assets/tree-icon.svg';

interface ClusterRegionSelectorProps {
  selectedItem: string;
  primaryClusterToRegionMap: any;
  asyncClusterToRegionMap: any;
  onClusterRegionSelected: (
    isCluster: boolean,
    isRegion: boolean,
    selectedOption: string,
    isPrimaryCluster: boolean
  ) => void;
}

const useStyles = makeStyles((theme) => ({
  selectBox: {
    minWidth: '250px'
  },
  boldText: {
    fontWeight: 500
  },
  regularText: {
    fontWeight: 300
  },
  menuItem: {
    display: 'block',
    padding: '15px 20px',
    height: '52px',
    whiteSpace: 'nowrap',
    fontSize: '14px'
  },
  overrideMuiInput: {
    '& .MuiInput-input': {
      fontWeight: 300,
      fontSize: '14px'
    },
    '& .MuiInput-root': {
      borderRadius: '8px 0px 0px 8px'
    }
  },
  icon: {
    marginRight: theme.spacing(1)
  }
}));

export const ClusterRegionSelector = ({
  selectedItem,
  primaryClusterToRegionMap,
  asyncClusterToRegionMap,
  onClusterRegionSelected
}: ClusterRegionSelectorProps) => {
  const classes = useStyles();

  const renderClusterAndRegionItems = (
    primaryClusterToRegionMap: any,
    asyncClusterToRegionMap: any
  ) => {
    const renderedItems: any = [];

    renderedItems.push(
      <MenuItem
        key={ALL_REGIONS}
        value={ALL_REGIONS}
        onClick={(e: any) => {
          onClusterRegionSelected(false, false, ALL_REGIONS, false);
        }}
        className={clsx(classes.menuItem, classes.regularText)}
      >
        {ALL_REGIONS}
      </MenuItem>
    );

    renderedItems.push(<Divider />);

    const primaryMapValues = primaryClusterToRegionMap.values();
    const primaryMapValueObject = primaryMapValues.next().value;
    const primaryCluster = primaryMapValueObject.cluster;
    const primaryClusterRegions = primaryMapValueObject.regions;

    // Add Primary Zones and Nodes
    if (isNonEmptyString(primaryCluster)) {
      renderedItems.push(
        <MenuItem
          key={primaryCluster}
          value={primaryCluster}
          onClick={(e: any) => {
            onClusterRegionSelected(true, false, primaryCluster, true);
          }}
          className={clsx(classes.menuItem, classes.boldText)}
        >
          {primaryCluster}
        </MenuItem>
      );
    }

    if (isNonEmptyArray(primaryClusterRegions)) {
      primaryClusterRegions.forEach((region: string) => {
        renderedItems.push(
          <MenuItem
            key={region}
            value={region}
            onClick={(e: any) => {
              onClusterRegionSelected(false, true, region, true);
            }}
            className={clsx(classes.menuItem, classes.regularText)}
          >
            <img
              className={classes.icon}
              src={treeIcon}
              alt="Indicator towards metric measure to use"
            />
            {region}
          </MenuItem>
        );
      });
    }

    // Add Read Replica Zones and Nodes
    if (asyncClusterToRegionMap?.size > 0) {
      renderedItems.push(<Divider />);

      const asyncMapValues = asyncClusterToRegionMap.values();
      const asyncMapValueObject = asyncMapValues.next().value;
      const asyncCluster = asyncMapValueObject.cluster;
      const asyncClusterRegions = asyncMapValueObject.regions;

      if (isNonEmptyString(asyncCluster)) {
        renderedItems.push(
          <MenuItem
            key={asyncCluster}
            value={asyncCluster}
            onClick={(e: any) => {
              onClusterRegionSelected(true, false, asyncCluster, true);
            }}
            className={clsx(classes.menuItem, classes.boldText)}
          >
            {asyncCluster}
          </MenuItem>
        );
      }

      if (isNonEmptyArray(asyncClusterRegions)) {
        asyncClusterRegions.forEach((region: string) => {
          renderedItems.push(
            <MenuItem
              key={region}
              value={region}
              onClick={(e: any) => {
                onClusterRegionSelected(false, true, region, true);
              }}
              className={clsx(classes.menuItem, classes.regularText)}
            >
              <img
                className={classes.icon}
                src={treeIcon}
                alt="Indicator towards metric measure to use"
              />
              {region}
            </MenuItem>
          );
        });
      }
    }

    return renderedItems;
  };

  return (
    <YBSelect
      className={clsx(classes.selectBox, classes.overrideMuiInput)}
      data-testid="cluster-region-select"
      value={selectedItem}
    >
      {primaryClusterToRegionMap.size > 0 &&
        renderClusterAndRegionItems(primaryClusterToRegionMap, asyncClusterToRegionMap)}
    </YBSelect>
  );
};

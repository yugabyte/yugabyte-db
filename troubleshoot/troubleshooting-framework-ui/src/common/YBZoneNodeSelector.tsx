import { MenuItem, makeStyles, Divider } from '@material-ui/core';
import clsx from 'clsx';
import { YBSelect, isNonEmptyArray, isNonEmptyObject } from '@yugabytedb/ui-components';

import treeIcon from '../assets/tree-icon.svg';

interface ClusterRegionSelectorProps {
  selectedItem: string;
  primaryZoneToNodesMap: any;
  asyncZoneToNodesMap: any;
  onZoneNodeSelected: (isZone: boolean, isNode: boolean, selectedOption: string) => void;
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
      borderRadius: '0px 8px 8px 0px'
    },
    '& .MuiMenu-list': {
      maxHeight: '400px'
    }
  },
  icon: {
    marginRight: theme.spacing(1)
  }
}));

const ALL_ZONES = 'All Zones and Nodes';

export const ZoneNodeSelector = ({
  selectedItem,
  primaryZoneToNodesMap,
  asyncZoneToNodesMap,
  onZoneNodeSelected
}: ClusterRegionSelectorProps) => {
  const classes = useStyles();

  const renderZoneAndNodeItems = (primaryZoneToNodesMap: any, asyncZoneToNodesMap: any) => {
    const renderedItems: any = [];

    renderedItems.push(
      <MenuItem
        key={ALL_ZONES}
        value={ALL_ZONES}
        onClick={(e: any) => {
          onZoneNodeSelected(false, false, ALL_ZONES);
        }}
        className={clsx(classes.menuItem, classes.regularText)}
      >
        {ALL_ZONES}
      </MenuItem>
    );

    renderedItems.push(<Divider />);

    const primaryMapValues = primaryZoneToNodesMap.values();
    let primaryMapiterator = primaryMapValues.next();

    while (isNonEmptyObject(primaryMapiterator.value)) {
      const primaryMapValueObject = primaryMapiterator.value;
      const primaryClusterZoneName = primaryMapValueObject.zoneName;
      const primaryClusternodeNames = primaryMapValueObject.nodeNames;

      if (primaryClusterZoneName) {
        renderedItems.push(
          <MenuItem
            key={primaryClusterZoneName}
            value={primaryClusterZoneName}
            onClick={(e: any) => {
              onZoneNodeSelected(true, false, primaryClusterZoneName);
            }}
            className={clsx(classes.menuItem, classes.boldText)}
          >
            {primaryClusterZoneName}
          </MenuItem>
        );
      }

      // Add Primary Zones and Nodes
      if (isNonEmptyArray(primaryClusternodeNames)) {
        primaryClusternodeNames.forEach((nodeName: string) => {
          renderedItems.push(
            <MenuItem
              key={nodeName}
              value={nodeName}
              onClick={(e: any) => {
                onZoneNodeSelected(false, true, nodeName);
              }}
              className={clsx(classes.menuItem, classes.regularText)}
            >
              <img
                className={classes.icon}
                src={treeIcon}
                alt="Indicator towards metric measure to use"
              />
              {nodeName}
            </MenuItem>
          );
        });
      }

      primaryMapiterator = primaryMapValues.next();
    }

    // Add Read Replica Zones and Nodes
    if (asyncZoneToNodesMap?.size > 0) {
      renderedItems.push(<Divider />);

      const asyncMapValues = asyncZoneToNodesMap.values();
      let asyncMapiterator = asyncMapValues.next();

      while (isNonEmptyObject(asyncMapiterator.value)) {
        const asyncMapValueObject = asyncMapiterator.value;
        const asyncClusterZoneName = asyncMapValueObject.zoneName;
        const asyncClusternodeNames = asyncMapValueObject.nodeNames;

        if (asyncClusterZoneName) {
          renderedItems.push(
            <MenuItem
              key={asyncClusterZoneName}
              value={asyncClusterZoneName}
              onClick={(e: any) => {
                onZoneNodeSelected(true, false, asyncClusterZoneName);
              }}
              className={clsx(classes.menuItem, classes.boldText)}
            >
              {asyncClusterZoneName}
            </MenuItem>
          );
        }

        if (isNonEmptyArray(asyncClusternodeNames)) {
          asyncClusternodeNames.forEach((nodeName: string) => {
            renderedItems.push(
              <MenuItem
                key={nodeName}
                value={nodeName}
                onClick={(e: any) => {
                  onZoneNodeSelected(false, true, nodeName);
                }}
                className={clsx(classes.menuItem, classes.regularText)}
              >
                <img
                  className={classes.icon}
                  src={treeIcon}
                  alt="Indicator towards metric measure to use"
                />
                {nodeName}
              </MenuItem>
            );
          });
        }

        asyncMapiterator = asyncMapValues.next();
      }
    }

    return renderedItems;
  };

  return (
    <YBSelect
      className={clsx(classes.selectBox, classes.overrideMuiInput)}
      data-testid="zone-node-select"
      value={selectedItem}
    >
      {renderZoneAndNodeItems(primaryZoneToNodesMap, asyncZoneToNodesMap)}
    </YBSelect>
  );
};

import { MenuItem, makeStyles, Divider } from '@material-ui/core';
import clsx from 'clsx';
import { YBSelect } from './YBSelect';
import TreeIcon from '../assets/tree-icon.svg';

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

    // Add Primary Zones and Nodes
    for (const [zoneName, zoneAttr] of primaryZoneToNodesMap.entries()) {
      renderedItems.push(
        <MenuItem
          key={zoneAttr.zoneName}
          value={zoneAttr.zoneName}
          onClick={(e: any) => {
            onZoneNodeSelected(true, false, zoneAttr.zoneName);
          }}
          className={clsx(classes.menuItem, classes.boldText)}
        >
          {zoneAttr.zoneName}
        </MenuItem>
      );
      zoneAttr.nodeNames.forEach((nodeName: string) => {
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
              src={TreeIcon}
              alt="Indicator towards metric measure to use"
            />
            {nodeName}
          </MenuItem>
        );
      });
    }

    if (asyncZoneToNodesMap?.size > 0) {
      renderedItems.push(<Divider />);
    }
    // Add Read Replica Zones and Nodes
    for (const [zoneName, zoneAttr] of asyncZoneToNodesMap.entries()) {
      renderedItems.push(
        <MenuItem
          key={zoneAttr.zoneName}
          value={zoneAttr.zoneName}
          onClick={(e: any) => {
            onZoneNodeSelected(true, false, zoneAttr.zoneName);
          }}
          className={clsx(classes.menuItem, classes.boldText)}
        >
          {zoneAttr.zoneName}
        </MenuItem>
      );
      zoneAttr.nodeNames.forEach((nodeName: string) => {
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
              src={TreeIcon}
              alt="Indicator towards metric measure to use"
            />
            {nodeName}
          </MenuItem>
        );
      });
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

import { useTranslation } from 'react-i18next';
import { mui, YBCheckbox, YBSelect } from '@yugabyte-ui-library/core';

import { RegionNodeGroup } from '../model/nodes';

const { styled, MenuItem, Typography, Divider } = mui;

const ALL_NODES_OPTION = '__allNodes__';
const REGION_OPTION_PREFIX = '__region__:';

const GroupMenuItem = styled(MenuItem)(({ theme }) => ({
  display: 'flex',
  justifyContent: 'space-between',
  gap: theme.spacing(2),
  '& .MuiFormControlLabel-label': {
    fontWeight: 600
  }
}));

export interface NodeScopeValue {
  allNodes: boolean;
  nodeNames: string[];
}

interface NodeScopeSelectProps {
  value: NodeScopeValue;
  onChange: (value: NodeScopeValue) => void;
  regions: RegionNodeGroup[];
  open: boolean;
  onOpenChange: (open: boolean) => void;
  dataTestId: string;
}

/**
 * Multi-select of nodes grouped by region. "All Nodes" and each region act as group toggles,
 * so the value MUI hands back is translated into a node list rather than used as is.
 */
export const NodeScopeSelect = ({
  value,
  onChange,
  regions,
  open,
  onOpenChange,
  dataTestId
}: NodeScopeSelectProps) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'universeActions.supportBundleV2.create.nodes'
  });
  const allNodeNames = regions.flatMap((region) => region.nodeNames);
  const selected = new Set(value.allNodes ? allNodeNames : value.nodeNames);

  const setSelection = (nodeNames: Set<string>) => {
    const isAll = allNodeNames.every((nodeName) => nodeNames.has(nodeName));
    onChange({
      allNodes: isAll,
      nodeNames: isAll ? [] : allNodeNames.filter((nodeName) => nodeNames.has(nodeName))
    });
  };

  const toggleGroup = (nodeNames: string[]) => {
    const next = new Set(selected);
    const isGroupSelected = nodeNames.every((nodeName) => next.has(nodeName));
    nodeNames.forEach((nodeName) => (isGroupSelected ? next.delete(nodeName) : next.add(nodeName)));
    setSelection(next);
  };

  const handleChange = (event: { target: { value: unknown } }) => {
    // The select's value holds only node names, so MUI returns it with the clicked option either
    // added (a group, or an unselected node) or removed (a selected node).
    const options = event.target.value as string[];
    const added = options.find((option) => !selected.has(option));
    if (added === ALL_NODES_OPTION) {
      toggleGroup(allNodeNames);
    } else if (added?.startsWith(REGION_OPTION_PREFIX)) {
      const region = added.slice(REGION_OPTION_PREFIX.length);
      toggleGroup(regions.find((group) => group.region === region)?.nodeNames ?? []);
    } else {
      const clicked = added ?? Array.from(selected).find((nodeName) => !options.includes(nodeName));
      if (clicked) {
        toggleGroup([clicked]);
      }
    }
  };

  const renderGroupOption = (option: string, label: string, nodeNames: string[]) => {
    const selectedCount = nodeNames.filter((nodeName) => selected.has(nodeName)).length;
    return (
      <GroupMenuItem key={option} value={option}>
        <YBCheckbox
          label={label}
          checked={selectedCount === nodeNames.length}
          indeterminate={selectedCount > 0 && selectedCount < nodeNames.length}
          dataTestId={`${dataTestId}-${option}`}
        />
        <Typography variant="subtitle1" color="text.secondary">
          {t('nodeCount', { count: nodeNames.length })}
        </Typography>
      </GroupMenuItem>
    );
  };

  return (
    <YBSelect
      label={t('label')}
      value={Array.from(selected)}
      onChange={handleChange}
      fullWidth
      dataTestId={dataTestId}
      renderValue={() =>
        value.allNodes ? t('allNodes') : t('selectedNodes', { count: value.nodeNames.length })
      }
      selectProps={{
        multiple: true,
        open,
        onOpen: () => onOpenChange(true),
        onClose: () => onOpenChange(false)
      }}
    >
      {renderGroupOption(ALL_NODES_OPTION, t('allNodes'), allNodeNames)}
      {regions.flatMap((group) => [
        <Divider key={`${group.region}-divider`} />,
        renderGroupOption(
          `${REGION_OPTION_PREFIX}${group.region}`,
          group.region || t('unknownRegion'),
          group.nodeNames
        ),
        ...group.nodeNames.map((nodeName) => (
          <MenuItem key={nodeName} value={nodeName}>
            <YBCheckbox
              label={nodeName}
              checked={selected.has(nodeName)}
              dataTestId={`${dataTestId}-${nodeName}`}
            />
          </MenuItem>
        ))
      ])}
    </YBSelect>
  );
};

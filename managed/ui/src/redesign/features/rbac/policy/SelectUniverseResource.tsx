/*
 * Created on Wed Aug 02 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useState } from 'react';
import { useToggle } from 'react-use';
import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { find, findIndex, isEqual } from 'lodash';
import { Box, makeStyles } from '@material-ui/core';
import { YBTable } from '../../../../components/backupv2/components/restore/pages/selectTables/YBTable';
import { YBCheckbox, YBModal } from '../../../components';
import { Universe } from '../../../helpers/dtos';
import { UniverseNameAndUUIDMapping } from './IPolicy';
import { Resource } from '../permission';
import { RbacUserWithResources } from '../users/interface/Users';

import Checked from '../../../assets/checkbox/Checked.svg';
import UnChecked from '../../../assets/checkbox/UnChecked.svg';
import Intermediate from '../../../assets/checkbox/Intermediate.svg';
import { ReactComponent as Create } from '../../../assets/edit_pen.svg';

type SelectUniverseResourceProps = {
  fieldIndex: number;
  universeList: Universe[];
};

const useStyle = makeStyles((theme) => ({
  root: {
    padding: theme.spacing(2)
  },
  includeUniverses: {
    float: 'right',
    marginBottom: theme.spacing(2),
    '& .MuiFormControlLabel-root': {
      marginRight: 0
    }
  },
  tableActions: {
    marginBottom: '2px !important',
    '& .search-input': {
      width: '430px !important'
    }
  },
  noRole: {
    border: `1px dashed ${theme.palette.primary[300]}`,
    background: theme.palette.primary[100],
    padding: `8px 16px`,
    minWidth: '458px',
    height: '42px',
    borderRadius: theme.spacing(1),
    color: '#67666C'
  },
  universeSelectionPane: {
    width: '454px',
    height: '42px',
    borderRadius: theme.spacing(1),
    padding: `${theme.spacing(1.5)}px ${theme.spacing(2)}px`,
    border: `1px dashed ${theme.palette.ybacolors.backgroundGrayDark}`,
    background: theme.palette.common.white,
    display: 'flex',
    justifyContent: 'space-between',
    alignItems: 'center'
  },
  editSelection: {
    cursor: 'pointer',
    display: 'flex',
    alignItems: 'center',
    '& svg': {
      marginRight: theme.spacing(1)
    }
  },
  universeCount: {
    width: theme.spacing(5),
    height: theme.spacing(3),
    border: '1px solid #E5E5E6',
    background: theme.palette.common.white,
    display: 'flex',
    alignItems: 'center',
    borderRadius: theme.spacing(0.75),
    justifyContent: 'center'
  },
  flex: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1)
  }
}));

export const SelectUniverseResource: FC<SelectUniverseResourceProps> = ({
  fieldIndex,
  universeList
}) => {
  const [showUniverseSelectionModal, toggleUniverseSelectionModal] = useToggle(false);
  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.policy.universeSelection'
  });
  const classes = useStyle();

  const { watch, setValue } = useFormContext<RbacUserWithResources>();

  const getUniverseLabelAndValue = (uuid: UniverseNameAndUUIDMapping[] | string[], all = false) => {
    let universes = [];
    // we get only the uuid from the backend
    if (uuid.length > 0 && typeof uuid[0] === 'string') {
      universes = universeList?.filter((u) => all || uuid.includes(u.universeUUID as any));
    } else {
      // we maintain uuid + label in the front end
      universes = universeList?.filter((u) => all || find(uuid, { universeUUID: u.universeUUID }));
    }
    return universes.map((t) => ({ name: t.name, universeUUID: t.universeUUID }));
  };

  const role = watch(`roleResourceDefinitions.${fieldIndex}.role`);
  const roleMappings = watch(`roleResourceDefinitions.${fieldIndex}.resourceGroup`);

  const ResourceGroupIndex = Math.max(
    findIndex(roleMappings.resourceDefinitionSet, { resourceType: Resource.UNIVERSE }),
    0
  );

  const [allowAll, setAllowAll] = useToggle(
    roleMappings.resourceDefinitionSet[ResourceGroupIndex].allowAll
  );

  const [selectedResources, setSelectedResources] = useState<UniverseNameAndUUIDMapping[]>(
    !allowAll
      ? getUniverseLabelAndValue(
          roleMappings.resourceDefinitionSet[ResourceGroupIndex].resourceUUIDSet
        )
      : getUniverseLabelAndValue([], true)
  );

  if (!role) {
    return <div className={classes.noRole}>{t('noRole')}</div>;
  }

  if (!find(role.permissionDetails.permissionList, { resourceType: Resource.UNIVERSE })) {
    return <div className={classes.noRole}>{t('noUniversePermInRole')}</div>;
  }

  const getUniverseSelectionText = () => {
    if (allowAll) {
      return t('allFutureUniverses');
    }
    return (
      <div className={classes.flex}>
        <span className={classes.universeCount}>
          {roleMappings.resourceDefinitionSet[ResourceGroupIndex]?.resourceUUIDSet.length}/
          {universeList.length}
        </span>
        {t('universes')}
      </div>
    );
  };
  return (
    <div>
      <div className={classes.universeSelectionPane}>
        <span>{getUniverseSelectionText()}</span>
        <div
          className={classes.editSelection}
          data-testid={`rbac-edit-universe-selection`}
          onClick={() => {
            toggleUniverseSelectionModal(true);
          }}
        >
          <Create />
          {t('editSelection')}
        </div>
      </div>
      <YBModal
        open={showUniverseSelectionModal}
        title={t('title.selectUniverse')}
        onClose={() => {
          toggleUniverseSelectionModal(false);
          const oldValues = !roleMappings.resourceDefinitionSet[ResourceGroupIndex].allowAll
            ? getUniverseLabelAndValue(
                roleMappings.resourceDefinitionSet[ResourceGroupIndex].resourceUUIDSet
              )
            : getUniverseLabelAndValue([], true);
          setAllowAll(roleMappings.resourceDefinitionSet[ResourceGroupIndex].allowAll);
          setSelectedResources(oldValues);
        }}
        overrideWidth={'900px'}
        overrideHeight={'720px'}
        size="xl"
        dialogContentProps={{
          dividers: true,
          className: classes.root
        }}
        submitLabel={t('confirm', { keyPrefix: 'common' })}
        cancelLabel={t('cancel', { keyPrefix: 'common' })}
        onSubmit={() => {
          setValue(
            `roleResourceDefinitions.${fieldIndex}.resourceGroup.resourceDefinitionSet.${ResourceGroupIndex}.resourceUUIDSet`,
            selectedResources
          );
          setValue(
            `roleResourceDefinitions.${fieldIndex}.resourceGroup.resourceDefinitionSet.${ResourceGroupIndex}.allowAll`,
            allowAll
          );
          toggleUniverseSelectionModal(false);
        }}
      >
        {showUniverseSelectionModal && (
          <YBTable<UniverseNameAndUUIDMapping>
            defaultValues={selectedResources}
            name={`universeUUIDList`}
            table={universeList?.map((t) => ({ name: t.name, universeUUID: t.universeUUID }))}
            setValue={(val) => {
              if (isEqual(val, selectedResources)) return;

              setSelectedResources(val);
              if (val.length !== universeList.length) {
                setAllowAll(false);
              }
            }}
            tableHeader={[t('universeName')]}
            searchPlaceholder={t('searchPlaceholder')}
            renderBodyFn={(universe) => <div>{universe.name}</div>}
            searchFn={(universe, searchText) => universe.name.includes(searchText)}
            tableCountInfo={(selected) => (
              <>
                {selected.length} / {universeList?.length}&nbsp;
                {t('selected', { keyPrefix: 'common' })}
              </>
            )}
            customComponents={() => (
              <Box className={classes.includeUniverses}>
                <YBCheckbox
                  name={`allowAll`}
                  checked={allowAll}
                  onChange={(e) => setAllowAll(e.target.value)}
                  label={t('includeFutureUniverse')}
                  icon={<img src={UnChecked} alt="unchecked" />}
                  checkedIcon={<img src={Checked} alt="checked" />}
                  indeterminateIcon={<img src={Intermediate} alt="intermediate" />}
                  disabled={selectedResources.length !== universeList.length}
                />
              </Box>
            )}
            overrideStyles={{
              actionsClassname: classes.tableActions
            }}
          />
        )}
      </YBModal>
    </div>
  );
};

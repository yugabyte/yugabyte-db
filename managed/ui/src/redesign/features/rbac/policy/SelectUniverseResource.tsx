/*
 * Created on Wed Aug 02 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useEffect, useState } from 'react';
import { useToggle } from 'react-use';
import { Controller, useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { find } from 'lodash';
import { Box, makeStyles } from '@material-ui/core';
import { YBTable } from '../../../../components/backupv2/components/restore/pages/selectTables/YBTable';
import { YBCheckboxField, YBModal } from '../../../components';
import { Universe } from '../../../helpers/dtos';
import { UniverseNameAndUUIDMapping } from './IPolicy';
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
    padding: `${theme.spacing(1.5)}px ${theme.spacing(2)}px`,
    width: '454px',
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

const ResourceGroupIndex = 0;

export const SelectUniverseResource: FC<SelectUniverseResourceProps> = ({
  fieldIndex,
  universeList
}) => {
  const [showUniverseSelectionModal, toggleUniverseSelectionModal] = useToggle(false);
  const { t } = useTranslation('translation', {
    keyPrefix: 'rbac.policy.universeSelection'
  });
  const classes = useStyle();

  const { control, watch, setValue } = useFormContext<RbacUserWithResources>();
  const roleUUID = watch(`roleResourceDefinitions.${fieldIndex}.roleUUID`);
  const roleMappings = watch(`roleResourceDefinitions.${fieldIndex}.resourceGroup`);
  
  const [selectedResources, setSelectedResources] = useState<UniverseNameAndUUIDMapping[]>(
    roleMappings.resourceDefinitionSet[ResourceGroupIndex].resourceUUIDSet
  );

  useEffect(() => {
    if (
      roleMappings.resourceDefinitionSet[ResourceGroupIndex].allowAll &&
      selectedResources.length !== universeList.length
    ) {
      setValue(
        `roleResourceDefinitions.${fieldIndex}.resourceGroup.resourceDefinitionSet.${ResourceGroupIndex}.allowAll`,
        false
      );
    }
  }, [
    roleMappings.resourceDefinitionSet[ResourceGroupIndex]?.allowAll,
    selectedResources.length,
    universeList,
    setValue,
    fieldIndex
  ]);

  if (!roleUUID) {
    return <div className={classes.noRole}>{t('noRole')}</div>;
  }

  const getUniverseSelectionText = () => {
    if (roleMappings?.resourceDefinitionSet[ResourceGroupIndex]?.allowAll) {
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
          toggleUniverseSelectionModal(false);
        }}
      >
        <Controller
          control={control}
          name={`roleResourceDefinitions.${fieldIndex}.resourceGroup.resourceDefinitionSet.${ResourceGroupIndex}.resourceUUIDSet`}
          render={({ field: { value } }) => {
            if (typeof value[0] === 'string') {
              value = value.map((universeUUID: any) => {
                const universe = find(universeList, { universeUUID });
                return { universeUUID: universeUUID, name: universe?.name ?? universeUUID };
              }) as any;
            }

            return (
              <YBTable<UniverseNameAndUUIDMapping>
                defaultValues={value}
                name={`mappings.${fieldIndex}.resource.universeUUIDList`}
                table={universeList?.map((t) => ({ name: t.name, universeUUID: t.universeUUID }))}
                setValue={setSelectedResources}
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
                    <YBCheckboxField
                      control={control}
                      name={`roleResourceDefinitions.${fieldIndex}.resourceGroup.resourceDefinitionSet.${ResourceGroupIndex}.allowAll`}
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
            );
          }}
        />
      </YBModal>
    </div>
  );
};

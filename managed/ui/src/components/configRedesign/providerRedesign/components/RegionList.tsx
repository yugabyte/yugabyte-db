/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import pluralize from 'pluralize';
import clsx from 'clsx';

import { isEmpty, split } from 'lodash';
import { EmptyListPlaceholder } from '../EmptyListPlaceholder';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { YBButton } from '../../../common/forms/fields';
import { YBPopover } from '../../../../redesign/components/YBPopover/YBPopover';
import { CloudVendorRegionField } from '../forms/configureRegion/ConfigureRegionModal';
import { ProviderCode, CloudVendorProviders } from '../constants';
import { K8sRegionField } from '../forms/configureRegion/ConfigureK8sRegionModal';
import { ConfigureOnPremRegionFormValues } from '../forms/configureRegion/ConfigureOnPremRegionModal';
import { RegionOperation } from '../forms/configureRegion/constants';
import { getRegionToInUseAz } from '../utils';
import { UniverseItem } from '../providerView/providerDetails/UniverseTable';
import {
  hasNecessaryPerm,
  RbacValidator
} from '../../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../../redesign/features/rbac/ApiAndUserPermMapping';

import { ValidationErrMsgDelimiter } from '../forms/utils';
import { SupportedRegionField } from '../forms/configureRegion/types';

import styles from './RegionList.module.scss';
import ErrorIcon from '../../../../redesign/assets/error.svg';


interface RegionListCommmonProps {
  showAddRegionFormModal: () => void;
  showEditRegionFormModal: (regionOperation: RegionOperation) => void;
  showDeleteRegionModal: () => void;
  disabled: boolean;

  providerUuid?: string;
  existingRegions?: string[];
  linkedUniverses?: UniverseItem[];
  isEditInUseProviderEnabled?: boolean;
  isError?: boolean;
  errors?: any[];
}
interface CloudVendorRegionListProps extends RegionListCommmonProps {
  providerCode: typeof CloudVendorProviders[number];
  regions: CloudVendorRegionField[];
  setRegionSelection: (regionSelection: CloudVendorRegionField) => void;
}
interface K8sRegionListProps extends RegionListCommmonProps {
  providerCode: typeof ProviderCode.KUBERNETES;
  regions: K8sRegionField[];
  setRegionSelection: (regionSelection: K8sRegionField) => void;
}
interface OnPremRegionListProps extends RegionListCommmonProps {
  providerCode: typeof ProviderCode.ON_PREM;
  regions: ConfigureOnPremRegionFormValues[];
  setRegionSelection: (regionSelection: ConfigureOnPremRegionFormValues) => void;
}

type RegionListProps = CloudVendorRegionListProps | K8sRegionListProps | OnPremRegionListProps;

export const RegionList = (props: RegionListProps) => {
  const { disabled, isError, regions, showAddRegionFormModal } = props;
  const { formatZones, formatRegionActions } = contextualHelpers(props);

  return regions.length === 0 && !disabled ? (
    <EmptyListPlaceholder
      actionButtonText={`Add Region`}
      descriptionText="Add regions to deploy DB nodes"
      onActionButtonClick={showAddRegionFormModal}
      className={clsx(isError && styles.emptyListError)}
      dataTestIdPrefix="RegionEmptyList"
    />
  ) : (
    <>
      <div className={styles.bootstrapTableContainer}>
        <BootstrapTable tableContainerClass={styles.bootstrapTable} data={regions}>
          <TableHeaderColumn dataField="name" isKey={true} dataSort={true}>
            Region
          </TableHeaderColumn>
          <TableHeaderColumn dataField="zones" dataFormat={formatZones}>
            Zones
          </TableHeaderColumn>
          <TableHeaderColumn
            columnClassName={styles.regionActionsColumn}
            dataFormat={formatRegionActions}
          />
        </BootstrapTable>
      </div>
    </>
  );
};

const contextualHelpers = ({
  providerUuid,
  disabled,
  linkedUniverses = [],
  isEditInUseProviderEnabled = false,
  existingRegions,
  providerCode,
  regions,
  setRegionSelection,
  showEditRegionFormModal,
  showDeleteRegionModal,
  errors = []
}: RegionListProps) => {
  const isProviderInUse = linkedUniverses.length > 0;
  const regionToInUseAz = providerUuid
    ? getRegionToInUseAz(providerUuid, linkedUniverses)
    : new Map<string, Set<String>>();
  switch (providerCode) {
    case ProviderCode.AWS:
    case ProviderCode.AZU:
    case ProviderCode.GCP: {
      const handleViewRegion = (regionField: CloudVendorRegionField) => {
        setRegionSelection(regionField);
        showEditRegionFormModal(RegionOperation.VIEW);
      };
      const handleEditRegion = (regionField: CloudVendorRegionField) => {
        setRegionSelection(regionField);
        showEditRegionFormModal(
          existingRegions?.includes(regionField.code)
            ? RegionOperation.EDIT_EXISTING
            : RegionOperation.EDIT_NEW
        );
      };
      const handleDeleteRegion = (regionField: CloudVendorRegionField) => {
        setRegionSelection(regionField);
        showDeleteRegionModal();
      };
      const formatZones = (zones: typeof regions[number]['zones']) =>
        pluralize('zone', zones.length, true);
      const formatRegionActions = (_: unknown, row: CloudVendorRegionField, _extra: unknown, index: number) => {
        const isRegionInUse = !!regionToInUseAz.get(row.code);
        return (
          <div className={styles.buttonContainer}>
            {
              !isEmpty(errors[index]) ? <YBPopover hoverMsg={<div> {split(errors[index]?.message, ValidationErrMsgDelimiter).map(msg => <div>{msg}</div>)}</div>}>
                <img src={ErrorIcon} />
              </YBPopover> : null
            }
            {(isProviderInUse && !isEditInUseProviderEnabled) ||
              !hasNecessaryPerm(ApiPermissionMap.MODIFY_PROVIDER) ? (
              <YBButton
                btnText="View"
                btnClass="btn btn-default"
                btnType="button"
                onClick={(e: any) => {
                  handleViewRegion(row);
                  e.currentTarget.blur();
                }}
                data-testid="RegionList-ViewRegion"
              />
            ) : (
              <RbacValidator accessRequiredOn={ApiPermissionMap.MODIFY_PROVIDER} isControl>
                <YBButton
                  className={clsx(disabled && styles.disabledButton)}
                  btnIcon="fa fa-pencil"
                  btnText="Edit"
                  btnClass="btn btn-default"
                  btnType="button"
                  onClick={(e: any) => {
                    handleEditRegion(row);
                    e.currentTarget.blur();
                  }}
                  disabled={disabled}
                  data-testid="RegionList-EditRegion"
                />
              </RbacValidator>
            )}
            <RbacValidator accessRequiredOn={ApiPermissionMap.MODIFY_PROVIDER} isControl>
              <YBButton
                className={clsx(disabled && styles.disabledButton)}
                btnIcon="fa fa-trash"
                btnText="Delete"
                btnClass="btn btn-default"
                btnType="button"
                onClick={() => handleDeleteRegion(row)}
                disabled={disabled || isRegionInUse}
                data-testid="RegionList-DeleteRegion"
              />
            </RbacValidator>
          </div>
        );
      };
      return {
        handleEditRegion: handleEditRegion,
        handleDeleteRegion: handleDeleteRegion,
        formatZones: formatZones,
        formatRegionActions: formatRegionActions
      };
    }
    case ProviderCode.KUBERNETES: {
      const handleViewRegion = (regionField: K8sRegionField) => {
        setRegionSelection(regionField);
        showEditRegionFormModal(RegionOperation.VIEW);
      };
      const handleEditRegion = (regionField: K8sRegionField) => {
        setRegionSelection(regionField);
        showEditRegionFormModal(
          existingRegions?.includes(regionField.code)
            ? RegionOperation.EDIT_EXISTING
            : RegionOperation.EDIT_NEW
        );
      };
      const handleDeleteRegion = (regionField: K8sRegionField) => {
        setRegionSelection(regionField);
        showDeleteRegionModal();
      };
      const formatZones = (zones: typeof regions[number]['zones']) =>
        pluralize('zone', zones.length, true);
      const formatRegionActions = (_: unknown, row: K8sRegionField, _extra: unknown, index: number) => {
        const isRegionInUse = !!regionToInUseAz.get(row.code);
        return (
          <div className={styles.buttonContainer}>
            {
              !isEmpty(errors[index]) ? <YBPopover hoverMsg={<div> {split(errors[index]?.message, ValidationErrMsgDelimiter).map(msg => <div>{msg}</div>)}</div>}>
                <img src={ErrorIcon} />
              </YBPopover> : null
            }
            {(isProviderInUse && !isEditInUseProviderEnabled) ||
              !hasNecessaryPerm(ApiPermissionMap.MODIFY_PROVIDER) ? (
              <YBButton
                btnText="View"
                btnClass="btn btn-default"
                btnType="button"
                onClick={(e: any) => {
                  handleViewRegion(row);
                  e.currentTarget.blur();
                }}
                data-testid="RegionList-ViewRegion"
              />
            ) : (
              <RbacValidator accessRequiredOn={ApiPermissionMap.MODIFY_PROVIDER} isControl>
                <YBButton
                  className={clsx(disabled && styles.disabledButton)}
                  btnIcon="fa fa-pencil"
                  btnText="Edit"
                  btnClass="btn btn-default"
                  btnType="button"
                  onClick={(e: any) => {
                    handleEditRegion(row);
                    e.currentTarget.blur();
                  }}
                  disabled={disabled}
                  data-testid="RegionList-EditRegion"
                />
              </RbacValidator>
            )}
            <RbacValidator accessRequiredOn={ApiPermissionMap.MODIFY_PROVIDER} isControl>
              <YBButton
                className={clsx(disabled && styles.disabledButton)}
                btnIcon="fa fa-trash"
                btnText="Delete"
                btnClass="btn btn-default"
                btnType="button"
                onClick={() => handleDeleteRegion(row)}
                disabled={disabled || isRegionInUse}
                data-testid="RegionList-DeleteRegion"
              />
            </RbacValidator>
          </div>
        );
      };
      return {
        handleEditRegion: handleEditRegion,
        handleDeleteRegion: handleDeleteRegion,
        formatZones: formatZones,
        formatRegionActions: formatRegionActions
      };
    }
    case ProviderCode.ON_PREM: {
      const handleViewRegion = (regionField: ConfigureOnPremRegionFormValues) => {
        setRegionSelection(regionField);
        showEditRegionFormModal(RegionOperation.VIEW);
      };
      const handleEditRegion = (regionField: ConfigureOnPremRegionFormValues) => {
        setRegionSelection(regionField);
        showEditRegionFormModal(
          existingRegions?.includes(regionField.code)
            ? RegionOperation.EDIT_EXISTING
            : RegionOperation.EDIT_NEW
        );
      };
      const handleDeleteRegion = (regionField: ConfigureOnPremRegionFormValues) => {
        setRegionSelection(regionField);
        showDeleteRegionModal();
      };
      const formatZones = (zones: typeof regions[number]['zones']) =>
        pluralize('zone', zones.length, true);
      const formatRegionActions = (_: unknown, row: ConfigureOnPremRegionFormValues) => {
        const isRegionInUse = !!regionToInUseAz.get(row.code);
        return (
          <div className={styles.buttonContainer}>
            {(isProviderInUse && !isEditInUseProviderEnabled) ||
              !hasNecessaryPerm(ApiPermissionMap.MODIFY_PROVIDER) ? (
              <YBButton
                btnText="View"
                btnClass="btn btn-default"
                btnType="button"
                onClick={(e: any) => {
                  handleViewRegion(row);
                  e.currentTarget.blur();
                }}
                data-testid="RegionList-ViewRegion"
              />
            ) : (
              <RbacValidator accessRequiredOn={ApiPermissionMap.MODIFY_PROVIDER} isControl>
                <YBButton
                  className={clsx(disabled && styles.disabledButton)}
                  btnIcon="fa fa-pencil"
                  btnText="Edit"
                  btnClass="btn btn-default"
                  btnType="button"
                  onClick={(e: any) => {
                    handleEditRegion(row);
                    e.currentTarget.blur();
                  }}
                  disabled={disabled}
                  data-testid="RegionList-EditRegion"
                />
              </RbacValidator>
            )}
            <RbacValidator accessRequiredOn={ApiPermissionMap.MODIFY_PROVIDER} isControl>
              <YBButton
                className={clsx(disabled && styles.disabledButton)}
                btnIcon="fa fa-trash"
                btnText="Delete"
                btnClass="btn btn-default"
                btnType="button"
                onClick={() => handleDeleteRegion(row)}
                disabled={disabled || isRegionInUse}
                data-testid="RegionList-DeleteRegion"
              />
            </RbacValidator>
          </div>
        );
      };
      return {
        handleEditRegion: handleEditRegion,
        handleDeleteRegion: handleDeleteRegion,
        formatZones: formatZones,
        formatRegionActions: formatRegionActions
      };
    }

    default:
      return {
        handleEditRegion: (regionField: SupportedRegionField) => null,
        handleDeleteRegion: (regionField: SupportedRegionField) => null,
        formatZones: (zones: typeof regions[number]['zones']) => '',
        formatRegionActions: (_: unknown, row: SupportedRegionField) => ''
      };
  }
};

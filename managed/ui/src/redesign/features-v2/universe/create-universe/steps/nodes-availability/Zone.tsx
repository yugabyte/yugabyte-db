import { FC, useContext, useState } from 'react';
import { values } from 'lodash';
import { Control, Controller, useWatch } from 'react-hook-form';
import { YBInput, YBSelect, mui } from '@yugabyte-ui-library/core';
import { PreferredInfoModal } from './index';
import { CreateUniverseContext, CreateUniverseContextMethods } from '../../CreateUniverseContext';
import { NodeAvailabilityProps } from './dtos';
import { Region } from '../../../../../features/universe/universe-form/utils/dto';
import { FaultToleranceType, ResilienceFormMode } from '../resilence-regions/dtos';

//icons
import { HelpOutline } from '@material-ui/icons';
import Return from '../../../../../assets/tree.svg';
import RemoveIcon from '../../../../../assets/close-large.svg';

const { MenuItem, Typography } = mui;

interface ZoneProps {
  control: Control<NodeAvailabilityProps>;
  index: number;
  region: Region;
  remove: () => void;
}

const menuProps = {
  anchorOrigin: {
    vertical: 'bottom',
    horizontal: 'left'
  },
  transformOrigin: {
    vertical: 'top',
    horizontal: 'left'
  }
} as any;

const StyledPreferedMenuItem = mui.styled(MenuItem)(({ theme }) => ({
  display: 'flex',
  flexDirection: 'column',
  padding: '16px !important',
  gap: '8px',
  height: 'auto',
  justifyContent: 'flex-start',
  alignItems: 'flex-start',
  width: '300px',
  '.MuiTypography-subtitle1': {
    color: theme.palette.grey[700]
  }
}));

export const Zone: FC<ZoneProps> = ({ control, index, region, remove }) => {
  const [{ resilienceAndRegionsSettings }] = (useContext(
    CreateUniverseContext
  ) as unknown) as CreateUniverseContextMethods;
  const isPrefferedAllowed = ![FaultToleranceType.NODE_LEVEL, FaultToleranceType.NONE].includes(
    resilienceAndRegionsSettings!.faultToleranceType
  );

  const [showPreferredInfoModal, setShowPreferredInfoModal] = useState(false);

  const availabilityZones = useWatch({ name: 'availabilityZones', control });

  const zonesCount = Object.keys(availabilityZones)?.reduce(
    (a, b) => a + availabilityZones[b].length,
    0
  );

  const maxPrefferedRankSelected = values(availabilityZones)
    .map((az) => az.map((zone) => zone.preffered))
    .flat()
    .reduce((a, b) => Math.max(a, b), -1);

  const preferredMenuItems = isPrefferedAllowed
    ? Array.from({ length: zonesCount }, (_, i) => (
        <StyledPreferedMenuItem key={i} value={i} disabled={i > maxPrefferedRankSelected + 1}>
          <Typography variant="body1">{`Rank ${i + 1}`}</Typography>
          <Typography variant="subtitle1">
            {i === 0 ? 'Default Preferred Zone' : 'Preferred zone if higher-rank zones fail.'}
          </Typography>
        </StyledPreferedMenuItem>
      ))
    : null;

  preferredMenuItems?.unshift(
    <StyledPreferedMenuItem key="no-option-selected" value={-1}>
      <Typography variant="body1">No</Typography>
      <Typography variant="subtitle1">Not Preferred</Typography>
    </StyledPreferedMenuItem>
  );

  const zone = useWatch({
    name: `availabilityZones.${region.code}.${index}`,
    control
  });

  return (
    <div style={{ display: 'flex', gap: '8px', alignItems: 'center', padding: '0px 8px' }}>
      <Return style={{ marginTop: '24px' }} />
      <Controller
        control={control}
        name={`availabilityZones.${region.code}.${index}`}
        render={({ field }) => (
          <YBSelect
            label="Availability Zone"
            sx={{ width: '300px' }}
            value={zone.name}
            onChange={(e) => {
              const selectedZone = region.zones.find((z) => z.name === e.target.value);
              if (selectedZone) {
                field.onChange({ ...field.value, ...selectedZone });
              }
            }}
            menuProps={menuProps}
            dataTestId="availability-zone-select"
          >
            {region.zones.map((zone) => (
              <MenuItem key={zone.uuid} value={zone.name}>
                {zone.name}
              </MenuItem>
            ))}
          </YBSelect>
        )}
      />
      <Controller
        control={control}
        name={`availabilityZones.${region.code}.${index}.nodeCount`}
        render={({ field }) => (
          <YBInput
            type="number"
            label="Nodes"
            value={field.value}
            onChange={(e) => {
              if (parseInt(e.target.value) < 1) {
                return;
              }
              field.onChange(parseInt(e.target.value));
            }}
            disabled={
              resilienceAndRegionsSettings?.resilienceFormMode === ResilienceFormMode.GUIDED &&
              isPrefferedAllowed
            }
            dataTestId="availability-zone-node-count-input"
          />
        )}
      />
      {isPrefferedAllowed && (
        <Controller
          control={control}
          name={`availabilityZones.${region.code}.${index}.preffered`}
          render={({ field }) => (
            <YBSelect
              label={
                <>
                  <span style={{ lineHeight: '16px' }}>Preferred</span>&nbsp;
                  {index === 0 && (
                    <HelpOutline
                      onClick={() => {
                        setShowPreferredInfoModal(true);
                      }}
                      style={{ cursor: 'pointer' }}
                    />
                  )}
                </>
              }
              sx={{ width: '90px' }}
              value={field.value}
              onChange={(e) => {
                field.onChange(e.target.value);
              }}
              menuProps={menuProps}
              renderValue={(value) => {
                return value === -1 ? 'No' : `Rank ${parseInt(value as string) + 1}`;
              }}
              dataTestId="availability-zone-preferred-select"
            >
              {preferredMenuItems}
            </YBSelect>
          )}
        />
      )}

      {isPrefferedAllowed && (
        <RemoveIcon
          style={{ marginTop: '24px', cursor: 'pointer', marginLeft: '8px' }}
          onClick={remove}
        />
      )}
      <PreferredInfoModal
        open={showPreferredInfoModal}
        onClose={() => {
          setShowPreferredInfoModal(false);
        }}
      />
    </div>
  );
};

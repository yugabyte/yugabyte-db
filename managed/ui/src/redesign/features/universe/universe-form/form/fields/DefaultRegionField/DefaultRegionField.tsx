import { ReactElement } from 'react';
import _ from 'lodash';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { useFormContext, useWatch } from 'react-hook-form';
import { Box, MenuItem } from '@material-ui/core';
import { YBHelper, YBLabel, YBSelectField, YBToggleField } from '../../../../../../components';
import { api, QUERY_KEY } from '../../../utils/api';
import { Region, UniverseFormData } from '../../../utils/dto';
import {
  DEFAULT_REGION_FIELD,
  MASTERS_IN_DEFAULT_REGION_FIELD,
  PROVIDER_FIELD,
  REGIONS_FIELD
} from '../../../utils/constants';

interface DefaultRegionsFieldProps {
  disabled?: boolean;
}

export const DefaultRegionField = ({ disabled }: DefaultRegionsFieldProps): ReactElement => {
  const { control } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();

  //watchers
  const provider = useWatch({ name: PROVIDER_FIELD }); //Listen to provider value change
  const regions = useWatch({ name: REGIONS_FIELD }); //regions selected in region field
  const { data: regionsData } = useQuery([QUERY_KEY.getRegionsList, provider?.uuid], () =>
    api.getRegionsList(provider?.uuid)
  );
  const regionsList = _.isEmpty(regionsData)
    ? []
    : _.sortBy(
        regionsData?.filter((region: Region) => regions.includes(region.uuid)),
        'name'
      ); //filter and show only regions that are selected in regions field

  return (
    <>
      <Box display="flex" width="100%" data-testid="DefaultRegionField-Container">
        <YBLabel dataTestId="DefaultRegionField-Label">
          {t('universeForm.cloudConfig.defaultRegion')}
        </YBLabel>
        <Box flex={1}>
          <YBSelectField
            name={DEFAULT_REGION_FIELD}
            control={control}
            fullWidth
            disabled={disabled}
            inputProps={{
              'data-testid': 'DefaultRegionField-Select'
            }}
          >
            {regionsList.map((region: Region) => (
              <MenuItem key={region.uuid} value={region.uuid}>
                {region.name}
              </MenuItem>
            ))}
          </YBSelectField>
        </Box>
      </Box>

      <Box display="flex" width="100%" mt={2}>
        <YBLabel dataTestId="DefaultRegionField-MastersLabel">
          {t('universeForm.cloudConfig.mastersInDefaultRegion')}
        </YBLabel>
        <Box flex={1}>
          <YBToggleField
            name={MASTERS_IN_DEFAULT_REGION_FIELD}
            inputProps={{
              'data-testid': 'DefaultRegionField-MastersToggle'
            }}
            control={control}
            disabled={disabled}
          />
          <YBHelper dataTestId="DefaultRegionField-MastersHelper">
            {t('universeForm.cloudConfig.mastersInDefaultRegionHelper')}
          </YBHelper>
        </Box>
      </Box>
    </>
  );
};

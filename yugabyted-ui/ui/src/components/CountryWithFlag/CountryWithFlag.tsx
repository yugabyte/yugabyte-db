import React, { FC } from 'react';
import { Box } from '@material-ui/core';
import { countryToFlag } from '@app/helpers';
import { useTranslation } from 'react-i18next';

interface CountryCodeOption {
  code: string;
  name: string;
  marketing_option: string;
}

interface CountryWithFlagProps {
  code: string;
  countries?: CountryCodeOption[]; // optional to not break while regions are loading
}

export const CountryWithFlag: FC<CountryWithFlagProps> = ({ code, countries = [] }) => {
  const { t } = useTranslation();
  const country = countries.find((item) => item.code === code);

  if (country) {
    return (
      <Box display="flex" alignItems="center">
        {countryToFlag(country.code)}
        <Box ml={1}>{country.name}</Box>
      </Box>
    );
  }

  return <span>{t('common.notAvailable')}</span>;
};

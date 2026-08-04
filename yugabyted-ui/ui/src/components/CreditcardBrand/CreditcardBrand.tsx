import React, { FC } from 'react';
import { Box } from '@material-ui/core';

import { YBTooltip } from '@app/components/YBTooltip/YBTooltip';

import GeneralCard from '@app/assets/General_Card.svg';
import Visa from '@app/assets/Visa.svg';
import Mastercard from '@app/assets/Mastercard.svg';
import AmericanExpress from '@app/assets/American_Express.svg';
import Jcb from '@app/assets/JCB.svg';
import UnionPay from '@app/assets/Union_Pay.svg';
import Discover from '@app/assets/Discover.svg';
import DinersClub from '@app/assets/Diners_Club.svg';

export enum CREDIT_CARD_BRANDS {
  VISA = 'visa',
  AMERICAN_EXPRESS = 'amex',
  DINERS_CLUB = 'diners',
  DISCOVER = 'discover',
  JCB = 'jcb',
  MASTERCARD = 'mastercard',
  UNION_PAY = 'unionpay'
}

interface CreditCardBrandProps {
  brand: string;
}
export const CreditCardBrand: FC<CreditCardBrandProps> = ({ brand }) => {
  let brandIcon;

  switch (brand) {
    case CREDIT_CARD_BRANDS.VISA:
      brandIcon = <Visa />;
      break;
    case CREDIT_CARD_BRANDS.AMERICAN_EXPRESS:
      brandIcon = <AmericanExpress />;
      break;
    case CREDIT_CARD_BRANDS.JCB:
      brandIcon = <Jcb />;
      break;
    case CREDIT_CARD_BRANDS.MASTERCARD:
      brandIcon = <Mastercard />;
      break;
    case CREDIT_CARD_BRANDS.UNION_PAY:
      brandIcon = <UnionPay />;
      break;
    case CREDIT_CARD_BRANDS.DISCOVER:
      brandIcon = <Discover />;
      break;
    case CREDIT_CARD_BRANDS.DINERS_CLUB:
      brandIcon = <DinersClub />;
      break;
    default:
      brandIcon = <GeneralCard />;
      break;
  }

  return (
    <Box>
      <YBTooltip title={brand} placement="top">
        <Box display="flex" alignItems="center" justifyContent="center">
          {brandIcon}
        </Box>
      </YBTooltip>
    </Box>
  );
};

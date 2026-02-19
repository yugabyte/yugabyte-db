// Copyright (c) YugabyteDB, Inc.
import moment from 'moment';
import { get } from 'lodash';
import { useSelector } from 'react-redux';
import { useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';

import { api, QUERY_KEY } from '../../../redesign/helpers/api';
import { apiV2, V2_QUERY_KEY } from '@app/redesign/helpers/apiV2';
import SlackLogo from './images/slack-logo-full.svg?img';
import githubLogo from './images/github-light-small.png';
import ybLogoImage from '../YBLogo/images/yb_ybsymbol_dark.png';
import YBLogo from '../YBLogo/YBLogo';
import DocsIcon from '../../../redesign/assets/book_open_blue.svg?img';
import FIPSIcon from '../../../redesign/assets/fips.svg?img';

import './stylesheets/Footer.scss';

export const Footer = () => {
  const { t } = useTranslation('translation', { keyPrefix: 'footer' });
  const ybaVersionResponse = useSelector((state) => state.customer?.yugawareVersion);
  const { data: haConfig } = useQuery(QUERY_KEY.getHAConfig, api.getHAConfig, {
    refetchInterval: 60_000
  });

  const data = useQuery(V2_QUERY_KEY.getFIPSInfo, apiV2.getFIPSInfo);

  const isFIPSEnabled = get(data, 'fips_enabled', false);

  const currentInstance = haConfig?.instances?.find((item) => item.is_local);
  const ybaVersion = ybaVersionResponse?.data?.version;
  const isCurrentOriginUnderDifferentAddress = window.location.origin !== currentInstance?.address;
  return (
    <footer>
      <div className="footer-metadata-container">
        <YBLogo type="monochrome" />
        {ybaVersion && (
          <span>
            {t('platformVersion')}: {ybaVersion}
          </span>
        )}
        {currentInstance?.address && isCurrentOriginUnderDifferentAddress && (
          <span>
            {t('platformHostUrl')}: {currentInstance.address}
          </span>
        )}

        {isFIPSEnabled && (
          <span>
            <img alt="FIPS" src={FIPSIcon} width="50" height="20" />
          </span>
        )}
      </div>
      <div className="footer-social-container">
        <a href="https://www.yugabyte.com/slack" target="_blank" rel="noopener noreferrer">
          <span className="social-media-cta">
            {t('joinUsOn')}
            <img alt="YugabyteDB Slack" src={SlackLogo} width="65" />
          </span>
        </a>
        <a
          href="https://github.com/yugabyte/yugabyte-db/"
          target="_blank"
          rel="noopener noreferrer"
        >
          <span className="social-media-cta">
            {t('starUsOn')}
            <img
              alt="YugabyteDB GitHub"
              className="social-media-logo"
              src={githubLogo}
              width="18"
            />{' '}
            <b>{t('github')}</b>
          </span>
        </a>
        <a
          href="https://www.yugabyte.com/community-rewards/"
          target="_blank"
          rel="noopener noreferrer"
        >
          <span className="social-media-cta">
            {t('freeTShirtAt')}
            <img
              alt="YugabyteDB Community Rewards"
              className="social-media-logo"
              src={ybLogoImage}
              width="85"
            />
          </span>
        </a>
        <a href="https://docs.yugabyte.com" target="_blank" rel="noopener noreferrer">
          <span className="social-media-cta">
            <span>Read docs</span>
            <img alt="YugabyteDB Docs" className="social-media-logo docs-logo" src={DocsIcon} width="15" />
          </span>
        </a>
      </div>
      <div className="copyright">
        &copy; {moment().get('year')} {t('yugabyteInc')}
      </div>
    </footer>
  );
};

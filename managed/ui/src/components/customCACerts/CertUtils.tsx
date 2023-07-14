import React from 'react';
import { isString } from 'lodash';
import { toast } from 'react-toastify';
import { CACert } from './ICerts';
import { isDefinedNotNull } from '../../utils/ObjectUtils';
import { createErrorMessage } from '../../redesign/features/universe/universe-form/utils/helpers';
import { TOAST_AUTO_DISMISS_INTERVAL } from '../../redesign/features/universe/universe-form/utils/constants';

const CACertErrorPatterns = [
  'PKIX path building failed',
  'No trust manager was able to validate this certificate chain'
];
export const downloadCert = (cert: CACert) => {
  const element = document.createElement('a');
  element.setAttribute(
    'href',
    'data:text/plain;charset=utf-8,' + encodeURIComponent(cert.contents)
  );
  element.setAttribute('download', cert.name + '.crt');

  element.style.display = 'none';
  document.body.appendChild(element);
  element.click();
  document.body.removeChild(element);
};

export const handleCACertErrMsg = (error: any) => {
  const errMsg = createErrorMessage(error);
  if (isDefinedNotNull(errMsg) && isString(errMsg) && CACertErrorPatterns.some(pattern => errMsg.includes(pattern))) {
    toast.error(
      <span>
        External CA is not present in YBA&apos;s trust-store. Please upload the CA cert &nbsp;
        <a
          href="/admin/custom-ca-certs"
          target="_blank"
          rel="noreferrer noopener"
          style={{
            color: 'white',
            textDecoration: 'underline'
          }}
        >
          here
        </a>
      </span>,
      { autoClose: TOAST_AUTO_DISMISS_INTERVAL }
    );
    return true;
  }
  return false;
};

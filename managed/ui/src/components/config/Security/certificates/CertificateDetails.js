// Copyright (c) YugaByte, Inc.

import React, { Fragment } from 'react';
import moment from 'moment';
import { YBModal } from '../../../common/forms/fields';

export const CertificateDetails = ({ certificate, visible, onHide }) => {
  const certStart = certificate.creationTime
    ? moment(certificate.creationTime).format('DD MMMM YYYY')
    : '';
  const certExpiry = certificate.expiryDate
    ? moment(certificate.expiryDate).format('DD MMMM YYYY')
    : '';

  const isVaultCert = certificate.type === 'HashicorpVault';

  const getCertDetails = () => (
    <>
      <li>
        <label>Certificate Name</label>
        <div>{certificate.name}</div>
      </li>
      <li>
        <label>Certificate Start</label>
        <div>{certStart}</div>
      </li>
      <li>
        <label>Certificate Expiration</label>
        <div>{certExpiry}</div>
      </li>
      <li>
        <label>Certificate</label>
        <div>{certificate.certificate}</div>
      </li>
      {certificate.privateKey && (
        <li>
          <label>Private Key</label>
          <div>{certificate.privateKey}</div>
        </li>
      )}
      {certificate.rootCertPath && (
        <Fragment>
          <li>
            <label>Root CA Certificate</label>
            <div>{certificate.rootCertPath}</div>
          </li>
          <li>
            <label>Database Node Certificate Path</label>
            <div>{certificate.nodeCertPath}</div>
          </li>
          <li>
            <label>Database Node Certificate Private Key</label>
            <div>{certificate.nodeKeyPath}</div>
          </li>
          <li>
            <label>Client Certificate</label>
            <div>{certificate.clientCertPath || '---'}</div>
          </li>
          <li>
            <label>Client Certificate Private Key</label>
            <div>{certificate.clientKeyPath || '---'}</div>
          </li>
        </Fragment>
      )}
    </>
  );

  const getVaultCertDetails = () => {
    const {
      hcVaultCertParams: { vaultAddr, vaultToken, engine, mountPath, role, ttl, ttlExpiry }
    } = certificate;

    const tokenExpiry = ttl && ttlExpiry ? moment(ttlExpiry).format('DD MMMM YYYY') : 'Wont Expire';

    return (
      <>
        <li>
          <label>Config Name</label>
          <div>{certificate.name}</div>
        </li>
        <li>
          <label>Certificate Start</label>
          <div>{certStart}</div>
        </li>
        <li>
          <label>Certificate Expiration</label>
          <div>{certExpiry}</div>
        </li>
        <li>
          <label>Vault Address</label>
          <div>{vaultAddr}</div>
        </li>
        <li>
          <label>Secret Token</label>
          <div>{vaultToken}</div>
        </li>
        <li>
          <label>Secret Engine</label>
          <div>{engine}</div>
        </li>
        <li>
          <label>Mount Path</label>
          <div>{mountPath}</div>
        </li>
        <li>
          <label>Role</label>
          <div>{role}</div>
        </li>
        <li>
          <label>Token Expiration</label>
          <div>{tokenExpiry}</div>
        </li>
      </>
    );
  };

  return (
    <div className="cert-details-modal">
      <YBModal
        title={'Certificate Details'}
        visible={visible}
        onHide={onHide}
        submitLabel={'Close'}
        onFormSubmit={onHide}
      >
        <ul className="cert-details-modal__list">
          {isVaultCert ? getVaultCertDetails() : getCertDetails()}
        </ul>
      </YBModal>
    </div>
  );
};

// Copyright (c) YugaByte, Inc.
//
// Author: Nishant Sharma(nissharma@deloitte.com)
//
// This file will hold all the details related to the channels
// for the respective destination.

import { isNonEmptyObject } from '../../../utils/ObjectUtils';
import { YBModal } from '../../common/forms/fields';

export const AlertDestinationDetails = ({ details, visible, onHide }) => {
  const showList = [];
  const showDetails = (details) => {
    const len = details.length;
    const hr = len > 1 && <hr />;
    details.forEach((detail, i) => {
      const commonDetails = (
        <>
          <li>
            <label>Target Type:</label>
            <div>{detail.channelType}</div>
          </li>
          <li>
            <label>Target Name:</label>
            <div>{detail.channelName}</div>
          </li>
        </>
      );
      const slackDetails = (
        <>
          {commonDetails}
          <li>
            <label>WebHookURL:</label>
            <div>{detail.webHookURL}</div>
          </li>
          {hr}
        </>
      );
      const webHookDetails = (
        <>
          {commonDetails}
          <li>
            <label>WebHookURL:</label>
            <div>{detail.webHookURL}</div>
          </li>
          {hr}
        </>
      );
      const pagerDutyDetails = (
        <>
          {commonDetails}
          <li>
            <label>PagerDuty API Key:</label>
            <div>{detail.apiKey}</div>
          </li>
          <li>
            <label>PagerDuty Service Integration Key:</label>
            <div>{detail.routingKey}</div>
          </li>
          {hr}
        </>
      );
      const emailDetails = (
        <>
          {commonDetails}
          <li>
            <label>Recipient:</label>
            <div>{detail.recipients}</div>
          </li>
          <li>
            <label>Default Recipients:</label>
            <div>{String(detail?.params?.defaultRecipients)}</div>
          </li>
          <li>
            <label>Default SMTP Settings:</label>
            <div>{String(detail?.params?.defaultSmtpSettings)}</div>
          </li>
          {detail?.params?.smtpData && (
            <>
              <li>
                <h5>SMTP Settings : </h5>
              </li>
              <li>
                <label>SMTP Server:</label>
                <div>{detail?.params?.smtpData?.smtpServer}</div>
              </li>
              <li>
                <label>SMTP Port:</label>
                <div>{detail?.params?.smtpData?.smtpPort}</div>
              </li>
              <li>
                <label>Email From:</label>
                <div>{detail?.params?.smtpData?.emailFrom}</div>
              </li>
              <li>
                <label>Username:</label>
                <div>{detail?.params?.smtpData?.smtpUsername}</div>
              </li>
              <li>
                <label>Password:</label>
                <div>{detail?.params?.smtpData?.smtpPassword}</div>
              </li>
              <li>
                <label>SSL:</label>
                <div>{String(detail?.params?.smtpData?.useSSL)}</div>
              </li>
              <li>
                <label>TLS:</label>
                <div>{String(detail?.params?.smtpData?.useTLS)}</div>
              </li>
            </>
          )}
          {hr}
        </>
      );

      const details =
        detail.channelType === 'Email'
          ? emailDetails
          : detail.channelType === 'Slack'
          ? slackDetails
          : detail.channelType === 'PagerDuty'
          ? pagerDutyDetails
          : detail.channelType === 'WebHook'
          ? webHookDetails
          : null;
      showList.push(
        <ul
          key={`${detail.channelName}-${detail.channelType}`}
          className="cert-details-modal__list alertDestinationDetail"
        >
          {details}
        </ul>
      );
    });
  };

  return (
    <div className="cert-details-modal">
      <YBModal
        title="Channel Details"
        visible={visible}
        onHide={onHide}
        submitLabel="Close"
        onFormSubmit={onHide}
      >
        {isNonEmptyObject(details) && showDetails(details)}
        {showList}
      </YBModal>
    </div>
  );
};

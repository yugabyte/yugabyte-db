// Copyright (c) YugabyteDB, Inc.

import { ToastContainer } from 'react-toastify';
import { ToastNotificationDuration } from '../redesign/helpers/constants';
import 'bootstrap/dist/css/bootstrap.css';
import 'react-toastify/dist/ReactToastify.css';
import './stylesheets/App.scss';
import './../_style/fonts.css';
import { GlobalRestoreModal } from '@app/components/restore/GlobalRestoreModal';

export const App = (props) => (
  <>
    <div>{props.children}</div>
    <ToastContainer
      hideProgressBar
      position="top-center"
      autoClose={ToastNotificationDuration.DEFAULT}
    />
    <GlobalRestoreModal />
  </>
);

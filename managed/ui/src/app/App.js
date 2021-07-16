// Copyright (c) YugaByte, Inc.

import React from 'react';
import { ToastContainer } from 'react-toastify';
import 'bootstrap/dist/css/bootstrap.css';
import 'react-toastify/dist/ReactToastify.css';
import './stylesheets/App.scss';
import './../_style/fonts.css';

export const App = (props) => (
  <>
    <div>{props.children}</div>
    <ToastContainer hideProgressBar position="top-center" autoClose={10000} />
  </>
);

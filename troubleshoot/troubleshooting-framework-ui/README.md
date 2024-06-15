# yugabytedb/troubleshoot-ui

Troubleshoot UI for Yugabyte applications

## install
```sh
npm i @yugabytedb/troubleshoot-ui
```

## use
```js
import { TroubleshootAdvisor } from '@yugabytedb/troubleshoot-ui';

<TroubleshootAdvisor universeUuid={universeUuid} timezone={timezone} />
```

Need to pass the universeUuid or clusterUuid that is assigned to the current universe or cluster

Pass user time zone so as to show anomalies in the primary dashboard based on the user current time


Selecting the anomaly to view the secondary dashboard will list the main graph which can be viewed in both Overall or Outlier mode

The corresponding RCA graphs will also be listed in secondary dashboard to help user diagnose the issue

## license

The MIT License (MIT)
Copyright (c) 2024 Rajagopalan Madhavan

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

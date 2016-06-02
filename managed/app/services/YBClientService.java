/**
 * Copyright (c) YugaByte, Inc.
 *
 * Created by ram on 6/1/16.
 */

package services;

import org.yb.client.YBClient;

public interface YBClientService {
    YBClient getClient();
}

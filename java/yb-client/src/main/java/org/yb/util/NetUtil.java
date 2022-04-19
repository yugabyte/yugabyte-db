// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.util;

import com.google.common.base.Functions;
import com.google.common.base.Joiner;
import com.google.common.base.Splitter;
import com.google.common.collect.Iterables;
import com.google.common.collect.Lists;
import com.google.common.net.HostAndPort;
import org.yb.annotations.InterfaceAudience;
import org.yb.CommonNet.HostPortPB;

import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Networking related methods.
 */
@InterfaceAudience.Private
public class NetUtil {

  /**
   * Convert a list of {@link HostAndPort} objects to a comma separate string.
   * The inverse of {@link #parseStrings(String, int)}.
   *
   * @param hostsAndPorts A list of {@link HostAndPort} objects.
   * @return Comma separate list of "host:port" pairs.
   */
  public static String hostsAndPortsToString(List<HostAndPort> hostsAndPorts) {
    return Joiner.on(",").join(Lists.transform(hostsAndPorts, Functions.toStringFunction()));
  }

  /**
   * Parse a "host:port" pair into a {@link HostAndPort} object. If there is no
   * port specified in the string, then 'defaultPort' is used.
   *
   * @param addrString  A host or a "host:port" pair.
   * @param defaultPort Default port to use if no port is specified in addrString.
   * @return The HostAndPort object constructed from addrString.
   */
  public static HostAndPort parseString(String addrString, int defaultPort) {
    return addrString.indexOf(':') == -1 ? HostAndPort.fromParts(addrString, defaultPort) :
               HostAndPort.fromString(addrString);
  }

  /**
   * Parse a comma separated list of "host:port" pairs into a list of
   * {@link HostAndPort} objects. If no port is specified for an entry in
   * the comma separated list, then a default port is used.
   * The inverse of {@link #hostsAndPortsToString(List)}.
   *
   * @param commaSepAddrs The comma separated list of "host:port" pairs.
   * @param defaultPort   The default port to use if no port is specified.
   * @return A list of HostAndPort objects constructed from commaSepAddrs.
   */
  public static List<HostAndPort> parseStrings(final String commaSepAddrs, int defaultPort) {
    Iterable<String> addrStrings = Splitter.on(',').trimResults().split(commaSepAddrs);
    List<HostAndPort> hostsAndPorts = Lists.newArrayListWithCapacity(Iterables.size(addrStrings));
    for (String addrString : addrStrings) {
      HostAndPort hostAndPort = parseString(addrString, defaultPort);
      hostsAndPorts.add(hostAndPort);
    }
    return hostsAndPorts;
  }

  public static List<HostPortPB> parseStringsAsPB(final String commaSepAddrs)
      throws Exception {
    String[] addrStrings = commaSepAddrs.split(",");
    List<HostPortPB> hostPorts = Lists.newArrayListWithCapacity(addrStrings.length);
    for (String addrString : addrStrings) {
      String [] hostPortPiece = addrString.split(":");
      // Check for host:port format.
      if (hostPortPiece.length != 2) {
        throw new Exception("Invalid format for host:port : " + addrString);
      }
      String host = hostPortPiece[0];
      Integer port;
      try {
        port = Integer.parseInt(hostPortPiece[1]);
      } catch (Exception e) {
        throw new Exception("Invalid format for host:port : " + addrString);
      }
      hostPorts.add(HostPortPB.newBuilder().setHost(host).setPort(port).build());
    }
    return hostPorts;
  }

  /**
   * Checks if two comma separated list of host:port have the same elements.
   *
   * @param commaSepAddrsLeft  The comma separated list of "host:port" pairs.
   * @param commaSepAddrsRight The other comma separated list of "host:port" pairs.
   * @return True is they are the same list of "host:port" pairs.
   */
  public static boolean areSameAddresses(final String commaSepAddrsLeft,
                                         final String commaSepAddrsRight) {
    String[] addrStringsLeft = commaSepAddrsLeft.split(",");
    String[] addrStringsRight = commaSepAddrsRight.split(",");

    if (addrStringsLeft.length != addrStringsRight.length) {
      return false;
    }

    Set<String> leftList = new HashSet<String>(Arrays.asList(addrStringsLeft));
    for (String addr : addrStringsRight) {
      if (!leftList.contains(addr)) {
         return false;
      }
    }

    return true;
  }
}

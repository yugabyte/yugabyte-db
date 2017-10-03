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
package org.kududb.util;

import com.google.common.base.Functions;
import com.google.common.base.Joiner;
import com.google.common.base.Splitter;
import com.google.common.collect.Iterables;
import com.google.common.collect.Lists;
import com.google.common.net.HostAndPort;
import org.kududb.annotations.InterfaceAudience;

import java.util.List;

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
}

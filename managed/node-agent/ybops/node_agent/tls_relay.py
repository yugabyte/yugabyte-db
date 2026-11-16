#!/usr/bin/env python
#
# Copyright (c) YugabyteDB, Inc.
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

import ipaddress
import logging
import select
import socket
import ssl
import threading

RELAY_BUFFER_BYTES = 65536
# Bounds how long a relay thread takes to notice close().
RELAY_POLL_SEC = 1.0


def create_ssl_context(ca_path):
    """
    Context that validates the chain against ca_path but leaves the hostname check to
    is_common_name_match(), as ssl matches an IP only against an IP SAN.
    """
    context = ssl.create_default_context(cafile=ca_path)
    context.check_hostname = False
    context.verify_mode = ssl.CERT_REQUIRED
    # The node agent is a gRPC server, which requires HTTP/2 negotiated through ALPN.
    context.set_alpn_protocols(["h2"])
    return context


def connect_tls(context, ip, port, timeout_sec):
    sock = socket.create_connection((ip, port), timeout=timeout_sec)
    try:
        return context.wrap_socket(sock)
    except Exception:
        sock.close()
        raise


def _is_same_ip(value, ip):
    try:
        return ipaddress.ip_address(value.strip()) == ipaddress.ip_address(ip)
    except ValueError:
        return False


def san_matches_ip(cert, ip):
    """
    Returns True if gRPC core accepts the cert for the IP target, which it does only when a
    SAN equals the IP.
    """
    for kind, value in cert.get("subjectAltName", ()):
        if kind == "IP Address" and _is_same_ip(value, ip):
            return True
        if kind == "DNS" and value == ip:
            return True
    return False


def most_specific_common_name(cert):
    # The subject is in DER order, so the last CN is the most specific one.
    common_name = None
    for rdn in cert.get("subject", ()):
        for key, value in rdn:
            if key == "commonName":
                common_name = value
    return common_name


def match_pattern(pattern, host):
    """
    Port of MatchPattern in src/yb/rpc/secure_stream.cc. A '*' matches any run of characters
    within one DNS label. The comparison is case-insensitive.
    """
    p = 0
    h = 0
    while p < len(pattern) and h < len(host):
        if pattern[p] == "*":
            p += 1
            while h < len(host) and host[h] != ".":
                if match_pattern(pattern[p:], host[h:]):
                    return True
                h += 1
        elif pattern[p].lower() == host[h].lower():
            p += 1
            h += 1
        else:
            return False
    return p == len(pattern) and h == len(host)


def is_common_name_match(cert, ip):
    """
    Returns True if the verified peer cert is accepted for ip only through the CN fallback:
    no SAN matches ip and the most specific CN does. This is the rule of
    SecureRefiner::MatchEndpoint in src/yb/rpc/secure_stream.cc and of the Java client
    (NodeAgentTrustManager).
    """
    if not cert or san_matches_ip(cert, ip):
        return False
    common_name = most_specific_common_name(cert)
    return bool(common_name) and match_pattern(common_name, ip)


def _close_socket(sock):
    try:
        sock.close()
    except OSError:
        pass


class TlsRelay(object):
    """
    Loopback relay for a node agent whose server cert matches the IP only by CN. gRPC core
    rejects such a cert for an IP target and Python cannot plug in a verifier, so gRPC talks
    plaintext to this relay on 127.0.0.1, and the relay forwards each connection over TLS that
    it verifies itself. The auth token stays in per-call metadata, so a local process that
    connects to the relay gets no credentials.
    """

    def __init__(self, ip, port, ca_path, connect_timeout_sec):
        self.ip = ip
        self.port = port
        self.connect_timeout_sec = connect_timeout_sec
        self.context = create_ssl_context(ca_path)
        self.closed = threading.Event()
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen()
        self.local_port = self.listener.getsockname()[1]
        threading.Thread(target=self._accept_loop, name="node-agent-tls-relay",
                         daemon=True).start()

    @property
    def target(self):
        return "127.0.0.1:{}".format(self.local_port)

    def close(self):
        # Open relay connections end on EOF once the gRPC channel is closed, or within
        # RELAY_POLL_SEC otherwise.
        self.closed.set()
        _close_socket(self.listener)

    def _accept_loop(self):
        while not self.closed.is_set():
            try:
                client, _ = self.listener.accept()
            except OSError:
                if not self.closed.is_set():
                    logging.exception("Node agent TLS relay stopped accepting connections")
                return
            threading.Thread(target=self._serve, args=(client,),
                             name="node-agent-tls-relay-conn", daemon=True).start()

    def _serve(self, client):
        upstream = None
        try:
            upstream = connect_tls(self.context, self.ip, self.port, self.connect_timeout_sec)
            # Each connection is verified again, as the agent may restart with another cert.
            if not is_common_name_match(upstream.getpeercert(), self.ip):
                logging.error("Node agent cert for {} no longer matches the IP by CN only"
                              .format(self.ip))
                return
            self._pump(client, upstream)
        except Exception as e:
            if not self.closed.is_set():
                logging.warning("Node agent TLS relay connection to {}:{} ended: {}"
                                .format(self.ip, self.port, e))
        finally:
            _close_socket(client)
            if upstream is not None:
                _close_socket(upstream)

    def _pump(self, client, upstream):
        # One thread per connection: an SSL socket must not be read and written concurrently
        # from different threads. Reads from upstream are non-blocking so that a TLS record
        # without application data (e.g. a session ticket) cannot stall the other direction.
        upstream.setblocking(False)
        while not self.closed.is_set():
            if upstream.pending():
                readable = [upstream]
            else:
                readable, _, _ = select.select([client, upstream], [], [], RELAY_POLL_SEC)
            for sock in readable:
                if sock is client:
                    data = client.recv(RELAY_BUFFER_BYTES)
                    if not data:
                        return
                    self._send_tls(upstream, data)
                else:
                    try:
                        data = upstream.recv(RELAY_BUFFER_BYTES)
                    except ssl.SSLWantReadError:
                        continue
                    if not data:
                        return
                    client.sendall(data)

    def _send_tls(self, upstream, data):
        view = memoryview(data)
        while view:
            try:
                view = view[upstream.send(view):]
            except ssl.SSLWantWriteError:
                select.select([], [upstream], [], RELAY_POLL_SEC)
            except ssl.SSLWantReadError:
                select.select([upstream], [], [], RELAY_POLL_SEC)
            if self.closed.is_set():
                return

#!/bin/python3

import datetime
import ipaddress
import os
import shutil
import tempfile
import time
from concurrent import futures
from unittest import TestCase

import grpc
import jwt
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID

from ybops.node_agent.rpc import RpcClient
from ybops.node_agent.server_pb2 import PingResponse, UploadFileResponse
from ybops.node_agent.server_pb2_grpc import NodeAgentServicer, add_NodeAgentServicer_to_server
from ybops.node_agent.tls_relay import match_pattern

HOST = "127.0.0.1"
UPLOAD_BYTES = 5 * 1024 * 1024


def _new_key():
    return rsa.generate_private_key(public_exponent=65537, key_size=2048)


def _new_cert(subject_cn, key, issuer_cn, issuer_key, is_ca=False, san=None):
    # subject_cn may be a list of CNs, in DER order.
    subject_cns = subject_cn if isinstance(subject_cn, list) else [subject_cn]
    now = datetime.datetime.now(datetime.timezone.utc)
    builder = (x509.CertificateBuilder()
               .subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, cn)
                                        for cn in subject_cns]))
               .issuer_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, issuer_cn)]))
               .public_key(key.public_key())
               .serial_number(x509.random_serial_number())
               .not_valid_before(now - datetime.timedelta(hours=1))
               .not_valid_after(now + datetime.timedelta(days=1))
               .add_extension(x509.BasicConstraints(ca=is_ca, path_length=None), critical=True))
    if san is not None:
        builder = builder.add_extension(x509.SubjectAlternativeName([san]), critical=False)
    return builder.sign(issuer_key, hashes.SHA256())


def _pem(cert):
    return cert.public_bytes(serialization.Encoding.PEM)


def _key_pem(key):
    return key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                             serialization.NoEncryption())


class _Servicer(NodeAgentServicer):
    def __init__(self):
        self.auth_headers = []
        self.uploaded_bytes = 0

    def Ping(self, request, context):
        self.auth_headers.append(dict(context.invocation_metadata()).get("authorization"))
        return PingResponse()

    def UploadFile(self, request_iterator, context):
        for request in request_iterator:
            self.uploaded_bytes += len(request.chunkData)
        return UploadFileResponse()


class TestNodeAgentRpcClient(TestCase):
    def setUp(self):
        self.tmp_dir = tempfile.mkdtemp()
        self.ca_key = _new_key()
        self.ca_cert = _new_cert("test-ca", self.ca_key, "test-ca", self.ca_key, is_ca=True)
        self.ca_path = self._write("ca.crt", _pem(self.ca_cert))
        now = int(time.time())
        self.token = jwt.encode({"iat": now, "exp": now + 600}, "0" * 32, algorithm="HS256")
        self.server = None
        self.client = None

    def tearDown(self):
        if self.client is not None:
            self.client.close()
        if self.server is not None:
            self.server.stop(0)
        shutil.rmtree(self.tmp_dir)

    def test_common_name_without_san_uses_relay(self):
        servicer = self._start_server(HOST, None)
        client = self._connect(self.ca_path)
        self.assertIsNotNone(client.relay)
        self.assertTrue(client.is_server_ready())
        self.assertEqual([self.token], servicer.auth_headers)
        upload_path = self._write("upload.bin", os.urandom(UPLOAD_BYTES))
        client.put_file(upload_path, "/tmp/upload.bin")
        self.assertEqual(UPLOAD_BYTES, servicer.uploaded_bytes)

    def test_ip_san_uses_direct_channel(self):
        self._start_server("node", x509.IPAddress(ipaddress.ip_address(HOST)))
        client = self._connect(self.ca_path)
        self.assertIsNone(client.relay)
        self.assertTrue(client.is_server_ready())

    def test_common_name_mismatch_without_san(self):
        self._start_server("127.0.0.2", None)
        client = self._connect(self.ca_path)
        self.assertIsNone(client.relay)
        self.assertFalse(client.is_server_ready(timeout=2))

    def test_common_name_with_mismatched_san(self):
        self._start_server(HOST, x509.IPAddress(ipaddress.ip_address("127.0.0.2")))
        client = self._connect(self.ca_path)
        self.assertIsNotNone(client.relay)
        self.assertTrue(client.is_server_ready())

    def test_common_name_and_san_mismatch(self):
        self._start_server("127.0.0.3", x509.IPAddress(ipaddress.ip_address("127.0.0.2")))
        client = self._connect(self.ca_path)
        self.assertIsNone(client.relay)
        self.assertFalse(client.is_server_ready(timeout=2))

    def test_most_specific_common_name_used(self):
        self._start_server(["other", HOST], None)
        client = self._connect(self.ca_path)
        self.assertIsNotNone(client.relay)
        self.assertTrue(client.is_server_ready())
        client.close()
        self.server.stop(0)
        self._start_server([HOST, "other"], None)
        client = self._connect(self.ca_path)
        self.assertIsNone(client.relay)
        self.assertFalse(client.is_server_ready(timeout=2))

    def test_match_pattern(self):
        self.assertTrue(match_pattern("node1.example.com", "NODE1.example.com"))
        self.assertTrue(match_pattern("*.example.com", "node1.example.com"))
        self.assertTrue(match_pattern("node*.example.com", "node1.example.com"))
        self.assertFalse(match_pattern("*.example.com", "a.node1.example.com"))
        self.assertFalse(match_pattern("*.example.com", "example.com"))
        self.assertFalse(match_pattern("node1.example.com", "node2.example.com"))

    def test_common_name_without_san_untrusted_ca(self):
        self._start_server(HOST, None)
        other_ca_key = _new_key()
        other_ca_path = self._write("other-ca.crt", _pem(
            _new_cert("other-ca", other_ca_key, "other-ca", other_ca_key, is_ca=True)))
        client = self._connect(other_ca_path)
        self.assertIsNone(client.relay)
        self.assertFalse(client.is_server_ready(timeout=2))

    def _start_server(self, subject_cn, san):
        key = _new_key()
        cert = _new_cert(subject_cn, key, "test-ca", self.ca_key, san=san)
        servicer = _Servicer()
        self.server = grpc.server(futures.ThreadPoolExecutor(max_workers=4))
        add_NodeAgentServicer_to_server(servicer, self.server)
        self.port = self.server.add_secure_port(
            "{}:0".format(HOST), grpc.ssl_server_credentials([(_key_pem(key), _pem(cert))]))
        self.server.start()
        return servicer

    def _connect(self, ca_path):
        self.client = RpcClient({"ip": HOST, "port": self.port, "cert_path": ca_path,
                                 "auth_token": self.token})
        self.client.connect()
        return self.client

    def _write(self, name, data):
        path = os.path.join(self.tmp_dir, name)
        with open(path, "wb") as f:
            f.write(data)
        return path

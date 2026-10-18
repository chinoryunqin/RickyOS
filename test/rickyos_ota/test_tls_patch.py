"""Check the SDK trust guard and embedded CA; not a device handshake."""
import base64
import hashlib
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLIENT = ROOT / "freeink-sdk/libs/network/SecureNet/src/SecureClient.cpp"


class TrustTests(unittest.TestCase):
    """The vendored SDK's TLS client verifies the peer and the host name."""

    def setUp(self):
        self.source = CLIENT.read_text()

    def test_trust_load_failure_is_explicit(self):
        trust = self.source[self.source.index("RICKYOS_VERIFIED_TLS"):]
        trust = trust[:trust.index("wolfSSL_SetIOReadCtx")]
        self.assertIn("wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_PEER", trust)
        self.assertIn("!_rootCA ||", trust)
        self.assertIn("!= WOLFSSL_SUCCESS", trust)
        self.assertIn("return 0;", trust)
        self.assertNotIn("} else if (_rootCA) {", self.source)

    def test_host_name_is_checked_before_the_handshake(self):
        self.assertIn("if (!_insecure && wolfSSL_check_domain_name(ssl, host) != WOLFSSL_SUCCESS) {", self.source)
        self.assertLess(self.source.index("wolfSSL_check_domain_name"), self.source.index("wolfSSL_SetIOReadCtx"))

    def test_embedded_root_has_reviewed_fingerprint(self):
        source = (ROOT / "src/network/RickyOtaTrust.h").read_text()
        pem_body = re.search(r"-----BEGIN CERTIFICATE-----\s+(.*?)\s+-----END CERTIFICATE-----", source, re.S).group(1)
        der = base64.b64decode("".join(pem_body.split()), validate=True)
        self.assertEqual(hashlib.sha256(der).hexdigest(),
                         "96bcec06264976f37460779acf28c5a7cfe8a3c0aae11a8ffcee05c0bddf08c6")


if __name__ == "__main__":
    unittest.main()

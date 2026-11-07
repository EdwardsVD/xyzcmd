#!/bin/bash
# make_keystore.sh - buat kunci debug untuk menandatangani APK (skema v2).
# Kunci ini bukan rahasia produksi: hanya untuk APK debug yang di-install manual.
set -e
cd "$(dirname "$0")/.."
PY=${PY:-./.venv/bin/python}
mkdir -p keystore
if [ -f keystore/debug.key.pem ] && [ -f keystore/debug.cert.der ]; then
  echo "keystore sudah ada"
  exit 0
fi
$PY - <<'PY'
from cryptography import x509
from cryptography.x509.oid import NameOID
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
import datetime

key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
open("keystore/debug.key.pem", "wb").write(key.private_bytes(
    serialization.Encoding.PEM, serialization.PrivateFormat.TraditionalOpenSSL,
    serialization.NoEncryption()))
name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "Nusa Rush Debug"),
                  x509.NameAttribute(NameOID.ORGANIZATION_NAME, "Arena Lab")])
now = datetime.datetime(2026, 1, 1)
cert = (x509.CertificateBuilder()
        .subject_name(name).issuer_name(name).public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now).not_valid_after(datetime.datetime(2046, 1, 1))
        .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
        .sign(key, hashes.SHA256()))
open("keystore/debug.cert.der", "wb").write(cert.public_bytes(serialization.Encoding.DER))
print("keystore dibuat: keystore/debug.key.pem + debug.cert.der")
PY

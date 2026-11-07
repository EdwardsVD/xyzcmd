#!/usr/bin/env python3
"""verify_apk_sign.py - periksa tanda tangan v2 APK secara mandiri (tanpa apksigner).

Cek: struktur APK Signing Block, digest isi zip, sertifikat, dan tanda tangan RSA.
"""
import hashlib
import struct
import sys

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding


def lp(b, o):
    n, = struct.unpack("<I", b[o:o + 4])
    return b[o + 4:o + 4 + n], o + 4 + n


def main(path):
    d = open(path, "rb").read()
    eocd = d.rfind(b"PK\x05\x06")
    cd_new, = struct.unpack("<I", d[eocd + 16:eocd + 20])
    cd_size, = struct.unpack("<I", d[eocd + 12:eocd + 16])
    # offset CD di EOCD menunjuk CD sesudah blok (spec v2). Blok dicari dari magic-nya.
    magic_pos = d.rfind(b"APK Sig Block 42", 0, cd_new)
    assert magic_pos > 0, "magic blok tanda tangan tidak ditemukan"
    size, = struct.unpack("<Q", d[magic_pos - 8:magic_pos])
    # tata letak: [size(8)][pasangan][size(8)][magic(16)] -> awal blok
    start = magic_pos + 8 - size
    size, = struct.unpack("<Q", d[start:start + 8])
    magic = d[magic_pos:magic_pos + 16]
    assert magic == b"APK Sig Block 42", f"magic salah: {magic!r}"
    assert struct.unpack("<Q", d[start:start + 8])[0] == size, "ukuran blok tidak konsisten"
    cd_real = start + size + 8
    assert cd_real == cd_new, f"offset CD di EOCD ({cd_new}) != akhir blok ({cd_real})"

    pos, end, pairs = start + 8, start + size - 16, {}
    while pos < end:
        vlen, kid = struct.unpack("<QI", d[pos:pos + 12])
        pairs[kid] = d[pos + 12:pos + 8 + vlen]
        pos += 8 + vlen
    got = pairs.get(0x7109871A)
    assert got is not None, "blok v2 tidak ada"
    print(f"APK Signing Block: {size + 8} byte, blok v2 {len(got)} byte, "
          f"id lain: {[hex(k) for k in pairs if k != 0x7109871A]}")

    _n, = struct.unpack("<I", got[:4])
    signer, _ = lp(got, 4)
    signed_data, p = lp(signer, 0)
    sigs, p = lp(signer, p)
    pub, p = lp(signer, p)
    digests_b, q = lp(signed_data, 0)
    certs_b, q = lp(signed_data, q)
    ndig, = struct.unpack("<I", digests_b[:4])        # jumlah digest
    a, dlen = struct.unpack("<II", digests_b[4:12])
    digest = digests_b[12:12 + dlen]
    cert_der, _ = lp(certs_b, 0)
    cert = x509.load_der_x509_certificate(cert_der)
    n, = struct.unpack("<I", sigs[:4])
    sa, slen = struct.unpack("<II", sigs[4:12])
    sig = sigs[12:12 + slen]

    e = bytearray(d[eocd:])
    e[16:20] = struct.pack("<I", start)   # apksig: digest pakai offset = awal blok
    secs = [d[0:start], d[cd_real:eocd], bytes(e)]
    assert len(d[cd_real:eocd]) == cd_size, "panjang CD tidak cocok dengan EOCD"
    digs = [hashlib.sha256(b"\xa5" + struct.pack("<I", len(s)) + s).digest() for s in secs]
    calc = hashlib.sha256(b"\x5a" + struct.pack("<I", len(digs)) + b"".join(digs)).digest()
    ok = calc == digest
    print("digest isi APK cocok:", ok, f"(sha256 {calc.hex()[:24]}...)")

    pk = cert.public_key()
    pk.verify(sig, signed_data, padding.PKCS1v15(), hashes.SHA256())
    print("tanda tangan RSA  : VALID", f"(algo 0x{sa:04x})")
    print("sertifikat        :", cert.subject.rfc4514_string(),
          "berlaku s/d", cert.not_valid_after.date())
    pub2 = pk.public_bytes(serialization.Encoding.DER,
                           serialization.PublicFormat.SubjectPublicKeyInfo)
    print("public key cocok  :", pub2 == pub)
    return 0 if calc == digest else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "output/nusa-rush-s3-debug.apk"))

#!/usr/bin/env python3
"""Generate self-signed TLS certificate for ESP Dashboard HTTPS/WSS server.

Usage: gen_cert.py <certs_dir> [<device_ip>]

  certs_dir  — directory where server.crt / server.key will be written
  device_ip  — optional static IP to add as an IP SAN (e.g. 192.168.1.42)

The certificate includes:
  • DNS SAN  esp-dashboard.local  (resolved by mDNS on the device)
  • DNS SAN  localhost
  • IP  SAN  127.0.0.1
  • IP  SAN  <device_ip>  (if provided)

Keeping the SAN list small is critical — a cert with hundreds of IP entries
exceeds the ESP32 mbedTLS record size and causes PR_END_OF_FILE_ERROR.

Regenerates if the existing cert is missing a SubjectAlternativeName extension.
Tries Python 'cryptography' library first; falls back to shelling out to openssl.
"""
import sys
import subprocess
from pathlib import Path


def _cert_has_san(cert_path: Path) -> bool:
    """Return True if the PEM cert already contains a SubjectAlternativeName."""
    try:
        from cryptography import x509
        cert = x509.load_pem_x509_certificate(cert_path.read_bytes())
        cert.extensions.get_extension_for_class(x509.SubjectAlternativeName)
        return True
    except Exception:
        return False


def _generate_with_cryptography(cert_path: Path, key_path: Path,
                                 extra_ip: str | None = None) -> None:
    from cryptography import x509
    from cryptography.x509.oid import NameOID
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import rsa
    from cryptography.x509 import DNSName, IPAddress
    import ipaddress
    import datetime

    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)

    name = x509.Name([
        x509.NameAttribute(NameOID.COMMON_NAME, "esp-dashboard.local"),
        x509.NameAttribute(NameOID.ORGANIZATION_NAME, "ESP Dashboard"),
    ])

    san_entries: list[x509.GeneralName] = [
        DNSName("esp-dashboard.local"),
        DNSName("localhost"),
        IPAddress(ipaddress.ip_address("127.0.0.1")),
    ]
    if extra_ip:
        san_entries.append(IPAddress(ipaddress.ip_address(extra_ip)))

    now = datetime.datetime.now(datetime.timezone.utc)
    cert = (
        x509.CertificateBuilder()
        .subject_name(name)
        .issuer_name(name)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now)
        .not_valid_after(now + datetime.timedelta(days=3650))
        .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
        .add_extension(x509.SubjectAlternativeName(san_entries), critical=False)
        .sign(key, hashes.SHA256())
    )

    cert_path.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    key_path.write_bytes(key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.TraditionalOpenSSL,
        serialization.NoEncryption(),
    ))
    size = cert_path.stat().st_size
    print(f"[gen_cert] Certificate size: {size} bytes")


def _san_string(extra_ip: str | None) -> str:
    parts = ["DNS:esp-dashboard.local", "DNS:localhost", "IP:127.0.0.1"]
    if extra_ip:
        parts.append(f"IP:{extra_ip}")
    return ",".join(parts)


def _generate_with_openssl(cert_path: Path, key_path: Path,
                            extra_ip: str | None = None) -> None:
    subprocess.run([
        "openssl", "req", "-x509", "-newkey", "rsa:2048",
        "-keyout", str(key_path), "-out", str(cert_path),
        "-days", "3650", "-nodes",
        "-subj", "/CN=esp-dashboard.local/O=ESP Dashboard",
        "-addext", f"subjectAltName={_san_string(extra_ip)}",
    ], check=True, capture_output=True)


def main() -> int:
    certs_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent.parent / "certs"
    extra_ip  = sys.argv[2] if len(sys.argv) > 2 else None

    cert_path = certs_dir / "server.crt"
    key_path  = certs_dir / "server.key"

    # Skip only when certs exist AND already have SAN (avoids infinite rebuilds)
    if cert_path.exists() and key_path.exists() and _cert_has_san(cert_path):
        print(f"[gen_cert] Certificates with SAN already present — skipping")
        return 0

    if cert_path.exists():
        print(f"[gen_cert] Regenerating: existing cert is missing SubjectAlternativeName")

    certs_dir.mkdir(parents=True, exist_ok=True)

    try:
        _generate_with_cryptography(cert_path, key_path, extra_ip)
        print(f"[gen_cert] Generated via 'cryptography' library: {cert_path}")
        return 0
    except ImportError:
        pass

    try:
        _generate_with_openssl(cert_path, key_path, extra_ip)
        print(f"[gen_cert] Generated via openssl: {cert_path}")
        return 0
    except subprocess.CalledProcessError as exc:
        detail = exc.stderr.decode(errors="replace") if exc.stderr else str(exc)
        print(f"[gen_cert] ERROR: openssl failed: {detail}", file=sys.stderr)
        print("[gen_cert] Install 'cryptography' (pip install cryptography) or ensure openssl is working",
              file=sys.stderr)
        return 1
    except FileNotFoundError:
        print("[gen_cert] ERROR: openssl not found on PATH", file=sys.stderr)
        print("[gen_cert] Install 'cryptography' (pip install cryptography) or add 'openssl' to PATH",
              file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

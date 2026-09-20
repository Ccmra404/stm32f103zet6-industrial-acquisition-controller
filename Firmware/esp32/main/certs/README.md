# Local TLS certificate

The ESP32 HTTPS console uses a local self-signed certificate. The certificate
and private key are not committed.

Generate both files before the first build:

```powershell
cd Firmware/esp32/main/certs
.\generate-dev-certs.ps1
```

The script creates:

```text
servercert.pem
serverkey.pem
```

Replace this certificate with a CA-issued certificate before production use.

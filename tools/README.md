# CLAWBOX deployment tooling

These are the files that make CLAWBOX a deployment host. They are copies of
what is installed there, kept in the repo so that the setup survives the
machine — if CLAWBOX is ever rebuilt, this directory is the recipe.

| File | Installed at | What it is |
|---|---|---|
| `workstation` | `/opt/workstation/bin/workstation`, symlinked into `/usr/local/bin` | build / publish / deploy / status CLI |
| `clawbox/nginx-workstation-ota.conf` | `/etc/nginx/sites-available/workstation-ota` | the HTTPS firmware endpoint on :8443 |
| `clawbox/workstation-backend.service` | `/etc/systemd/system/workstation-backend.service` | the FastAPI app on :8000 |

They are **copies, not the live files.** Editing them here changes nothing on
CLAWBOX until they are installed again. If you change one on CLAWBOX, copy it
back here in the same change, or the next person rebuilding from this
directory gets the old behaviour.

## Rebuilding CLAWBOX from scratch

```bash
sudo apt-get install -y git wget flex bison gperf python3 python3-venv cmake \
    ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0 jq openssl nginx

sudo mkdir -p /opt/workstation && sudo chown agent:agent /opt/workstation
sudo -u agent git clone --branch v5.5.3 --depth 1 --recursive \
    https://github.com/espressif/esp-idf.git /opt/workstation/esp-idf
sudo -u agent env IDF_TOOLS_PATH=/opt/workstation/.espressif \
    /opt/workstation/esp-idf/install.sh esp32s3

sudo -u agent git clone https://github.com/mshears713/workstation_esp32.git \
    /opt/workstation/repo
sudo -u agent mkdir -p /opt/workstation/artifacts/bin \
    /opt/workstation/artifacts/manifests /opt/workstation/bin
sudo install -o agent -g agent -m 0755 tools/workstation /opt/workstation/bin/workstation
sudo ln -sf /opt/workstation/bin/workstation /usr/local/bin/workstation
```

Then the TLS certificate the firmware pins — note the SANs, which must match
what `device_config.h` points at:

```bash
sudo mkdir -p /opt/workstation/tls && cd /opt/workstation/tls
sudo openssl req -x509 -newkey rsa:2048 -sha256 -days 3650 -nodes \
  -keyout clawbox_ota.key -out clawbox_ota.crt \
  -subj "/CN=clawbox.local/O=WORKSTATION OTA" \
  -addext "subjectAltName=DNS:clawbox.local,DNS:clawbox,IP:192.168.1.71" \
  -addext "basicConstraints=critical,CA:TRUE" \
  -addext "keyUsage=critical,digitalSignature,keyEncipherment,keyCertSign"
sudo chmod 600 clawbox_ota.key
```

A **new** certificate means `main/certs/clawbox_ota_ca.pem` has to be updated
and that firmware deployed *before* nginx starts serving it — see doc/OTA.md,
"Replacing CLAWBOX's certificate". Get that order wrong and wireless updates
stop until someone brings a USB cable.

Then the services, the firewall, and the one file git does not carry:

```bash
sudo cp tools/clawbox/nginx-workstation-ota.conf /etc/nginx/sites-available/workstation-ota
sudo ln -sf /etc/nginx/sites-available/workstation-ota /etc/nginx/sites-enabled/
sudo cp tools/clawbox/workstation-backend.service /etc/systemd/system/
sudo nginx -t && sudo systemctl daemon-reload
sudo systemctl enable --now nginx workstation-backend

sudo ufw allow from 192.168.0.0/16 to any port 8000 proto tcp
sudo ufw allow from 192.168.0.0/16 to any port 8443 proto tcp

# main/wifi_credentials.h is git-ignored; copy it in by hand or the build fails
scp main/wifi_credentials.h mshears@clawbox:/tmp/
ssh mshears@clawbox 'sudo -u agent install -m600 /tmp/wifi_credentials.h \
    /opt/workstation/repo/main/ && rm /tmp/wifi_credentials.h'
```

The backend is a **separate repository** and is not covered by the clone
above; see `backend/README.md`.

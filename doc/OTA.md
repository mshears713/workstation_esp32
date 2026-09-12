# WORKSTATION firmware: build, publish, deploy

How firmware gets onto WORKSTATION now that it no longer arrives over USB.

This document is the reference for anyone â€” person or agent â€” picking the
system up cold. It assumes nothing about the conversation that produced it.

---

## 1. The shape of the thing

```
  WORKSTATION                     CLAWBOX                      GitHub
  ESP32-S3-BOX-3                  Ubuntu 24.04, 192.168.1.71   canonical source
  16MB flash, dual OTA slots      always on, on the LAN
        |                               |                            |
        |  HTTP  :8000  app API  <------+                            |
        |  HTTPS :8443  firmware <------+                            |
        |                               +------ git pull ------------+
        |                               |
        +--- polls every 60s: "what should I be running?"
```

- **WORKSTATION** is the device. It pulls; nothing pushes to it.
- **CLAWBOX** holds the repo, the ESP-IDF build environment, the FastAPI
  backend, and the firmware artifacts. It is the deployment host.
- **This Windows laptop is no longer required for anything.** It is a
  development machine and a USB recovery station, nothing more.

### Why the device pulls

There is no agent on the device and no inbound port. CLAWBOX publishes a
statement of intent â€” "the version you should be running is X" â€” and the
device acts on it the next time it looks. That makes deployment a change to
one file on CLAWBOX rather than a network operation that has to succeed while
the device happens to be awake and reachable.

---

## 2. Build, publish, deploy are three different verbs

This is the most important idea here, so it gets its own section.

| Verb | What it does | Does it change what the device runs? |
|---|---|---|
| `workstation build` | compiles the repo | **no** |
| `workstation publish` | makes that binary downloadable and records its hash | **no** |
| `workstation deploy X` | names X as the desired version | **yes** |

A commit does not reach the hardware. A build does not reach the hardware.
Only `deploy` does, and only because the device reads what `deploy` writes.

There is no git hook, no CI trigger, and no "deploy on green". If that is ever
wanted, it should be an explicit thing somebody adds, not something that
happens because the pieces were left connected.

---

## 3. The everyday workflow

On CLAWBOX, as the `agent` user (OpenClaw already is):

```bash
workstation status                 # where things stand
cd /opt/workstation/repo && git pull
workstation build
workstation publish
workstation deploy latest          # the only step that touches the device
workstation status                 # confirm the device took it
```

`workstation status` prints the repo head, the last build, what is published,
what is deployed, and the device's own recent check-ins â€” including whether
the device has actually taken the deployed version yet.

### The shortest version

When the instruction is "update WORKSTATION with the latest code":

```bash
ssh mshears@clawbox
cd /opt/workstation/repo && git pull && workstation build && workstation publish && workstation deploy latest
```

Then wait up to ~60s and run `workstation status`. Done â€” no cable.

---

## 4. What happens on the device

1. Every 60 seconds the OTA task GETs
   `https://clawbox.local:8443/api/v1/firmware/desired`, appending its own
   state to the query string (`?current=â€¦&slot=â€¦&state=â€¦`). That query string
   is how CLAWBOX knows what the device is running â€” nginx logs it, and
   `workstation status` reads the log.
2. If the manifest names a version different from the running one, the device
   downloads that image into the **inactive** OTA slot, hashing as it writes.
3. It compares the SHA-256 it computed with the one in the manifest. A
   mismatch stops everything: the boot slot is not switched and the running
   firmware is untouched.
4. On a match it calls `esp_ota_set_boot_partition()` and reboots.
5. The new image boots **on probation** (`PENDING_VERIFY`). It must pass a
   health check before it is trusted.

### The health gate

In `main/ota_service.c`. The new image is only marked valid once all of
these hold, within 90 seconds:

- NVS opened and runtime config loaded
- the display and LVGL came up
- Wi-Fi associated
- the backend answered `/health` at least once

Then it calls `esp_ota_mark_app_valid_cancel_rollback()`.

If the 90 seconds elapse first, it calls
`esp_ota_mark_app_invalid_rollback_and_reboot()` and the device comes back on
the previous slot. If the image crashes outright, the bootloader does the same
thing on its own â€” an image that never marks itself valid does not get a
second boot.

The checks are deliberately few. Each one is something that, if broken, makes
the device useless in a way the previous image was not. The specific failure
this project has hit before â€” a change that starves internal RAM so
`esp_wifi_start()` fails â€” is caught by the Wi-Fi condition.

---

## 5. Partition layout

16MB flash, verified on the chip (`esptool flash_id` â†’ GigaDevice c8/6018).

| Offset | Name | Size | Notes |
|---|---|---|---|
| 0x000000 | bootloader | (32K available) | ~21K used |
| 0x008000 | partition table | 4K | |
| 0x009000 | `nvs` | 24K | **unchanged offset and size** |
| 0x00F000 | `phy_init` | 4K | |
| 0x010000 | `otadata` | 8K | new |
| 0x012000 | *(padding)* | 56K | app slots need 64K alignment |
| 0x020000 | `ota_0` | 5504K | |
| 0x580000 | `ota_1` | 5504K | |
| 0xAE0000 | `model` | 5248K | ESP-SR WakeNet + MultiNet |
| 0x1000000 | â€” | | end of flash, exactly |

At the time of the migration the application was 2,637,856 bytes â€” 47% of a
slot, so there is 2.1x headroom.

### Why no factory partition

Three app-sized regions do not fit comfortably. After the 5248K the speech
models need, about 10.6MB remains; split three ways that is ~3.5MB per slot,
barely above today's 2.5MB image and shrinking as ESP-SR and LVGL grow. With
two slots the previous known-good firmware **is** the recovery image, and the
bootloader falls back to it automatically. USB recovery still covers the cases
a factory app would not have survived anyway (a corrupt bootloader, a bad
partition table).

### Why `nvs` did not move

The Black Box (`boot_count` / `last_fault` / `last_cmd`, namespace from
Mission 07) and the runtime backend address written by `device_config.c` both
live in `nvs`. Moving the partition would discard both. Its offset and size
are the one part of the old table carried across untouched.

---

## 6. Runtime configuration â€” no more rebuilds to move the backend

`BACKEND_BASE_URL` used to be a string literal in `main/backend_config.h`
holding a laptop's DHCP address. It is now a call into `main/device_config.c`.

Precedence:

1. the value in NVS (namespace `wscfg`, key `backend_url`)
2. the compiled default, `http://clawbox.local:8000`

Same for the OTA endpoint (`ota_url`, default `https://clawbox.local:8443`).

A value written at runtime takes effect **on the next boot**, not instantly â€”
the accessor hands out a pointer into a static buffer that several tasks read
without locking, and rewriting it mid-request would be a real race for the
sake of skipping a reboot nobody minds.

`clawbox.local` resolves through lwIP's mDNS support
(`CONFIG_LWIP_DNS_SUPPORT_MDNS_QUERIES`, already enabled), so a DHCP change on
CLAWBOX does not require touching the device. If mDNS ever proves unreliable,
write a literal IP into NVS â€” that escape hatch is the reason this module
exists.

The FIRMWARE page on the device shows which backend address is actually in
use, so "what is it configured for" has an answer you can read off the screen
instead of inferring from the source you think you flashed.

---

## 7. What is protected, and what is not

**Protected**

- The firmware channel is HTTPS. CLAWBOX's certificate is compiled into the
  image (`main/certs/clawbox_ota_ca.pem`) and is the *only* certificate
  accepted â€” not a public CA, not the bundle. A different server cannot serve
  this device firmware.
- Every image is checked against the SHA-256 in the manifest before the boot
  slot is switched. Corruption or a swapped artifact is caught.
- The OTA endpoint is restricted to RFC1918 addresses in nginx.
- The device writes only through `esp_ota_write()`, into the slot ESP-IDF
  nominates. There is no code path that writes to an arbitrary flash address,
  and no API that would let CLAWBOX ask for one.

**Not protected**

- **The image itself is unsigned.** Anything that can write into
  `/opt/workstation/artifacts/` on CLAWBOX can publish firmware this device
  will install. The trust boundary is CLAWBOX's filesystem.
- Application traffic on `:8000` is plain HTTP, unchanged from before this
  migration. Anything on the LAN can read it.
- Certificate expiry is not checked (`CONFIG_MBEDTLS_HAVE_TIME_DATE` is off,
  which is the ESP-IDF default). This is deliberate: the device has no clock
  at boot, and a pinned self-signed certificate gains nothing from date
  validation.

**The next seam**, if the trust boundary should move off CLAWBOX's filesystem:
`CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT` plus
`CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT`. That makes the device verify
a signature over the image itself, so only a holder of the signing key can
produce firmware it will run. It was left out of this migration because it
changes bootloader behaviour and wants its own careful change with a USB
recovery path standing by.

### Replacing CLAWBOX's certificate

The certificate is valid until 2036. Replacing it earlier means:

1. generate the new pair into `/opt/workstation/tls/`
2. copy the new `.crt` into `main/certs/clawbox_ota_ca.pem`
3. build, publish and deploy that firmware **while the old certificate is
   still being served**
4. only then switch nginx to the new certificate

Getting that order wrong bricks wireless updates and means a USB cable.

---

## 8. What still needs USB

- **A partition table change.** Slots, sizes, offsets â€” anything in
  `partitions.csv`.
- **The bootloader.** Including turning secure boot or signed apps on.
- **ESP-SR speech models.** `srmodels.bin` is written by `idf.py flash`; it is
  not part of an application OTA and is intentionally treated as a separate
  concern. Changing the enabled WakeNet/MultiNet models means a USB flash.
- **Recovery** when both slots are bad, or NVS needs erasing.

Everything else â€” application code, UI, backend clients, the OTA logic
itself â€” goes over Wi-Fi.

### USB recovery procedure

From this repo on a machine with ESP-IDF 5.5.3, device on a USB port:

```
idf.py -p COM7 flash monitor         # Windows
idf.py -p /dev/ttyACM0 flash monitor # Linux
```

That writes bootloader, partition table, fresh `otadata`, the app into
`ota_0`, and the speech models. It does **not** erase NVS, so the Black Box
and the runtime backend address survive. To start genuinely clean, add
`erase-flash` first â€” and know that you are discarding both.

---

## 9. Where things live on CLAWBOX

| What | Path |
|---|---|
| firmware repo | `/opt/workstation/repo` |
| ESP-IDF v5.5.3 | `/opt/workstation/esp-idf` |
| IDF tools | `/opt/workstation/.espressif` |
| published images | `/opt/workstation/artifacts/bin/` |
| manifests | `/opt/workstation/artifacts/manifests/` |
| the deploy pointer | `/opt/workstation/artifacts/desired.json` |
| TLS cert + key | `/opt/workstation/tls/` |
| the CLI | `/opt/workstation/bin/workstation` (also on `$PATH`) |
| backend repo | `/home/agent/.openclaw/workspace/workstation-backend` |

Services:

| Unit | What |
|---|---|
| `workstation-backend.service` | the FastAPI app on `:8000` |
| `nginx.service` | the firmware endpoint on `:8443` |

```bash
systemctl status workstation-backend
journalctl -u workstation-backend -f
sudo tail -f /var/log/nginx/workstation-ota-access.log
```

The backend repo is a **separate repository**
(`github.com/mshears713/workstation-backend`). The `backend/` directory inside
*this* repo is an early single-file version and is not what runs â€” see the
note in that directory.

---

## 9a. Things a fresh CLAWBOX needs that git does not carry

Two of these cost an hour between them the first time. They are not bugs;
they are the parts of the setup that deliberately do not live in the repo.

**`main/wifi_credentials.h`.** Git-ignored, so a fresh clone does not have it
and the build fails with a clear `#error`. Copy it onto CLAWBOX by hand:

```bash
scp main/wifi_credentials.h mshears@clawbox:/tmp/ && \
  ssh mshears@clawbox 'sudo -u agent install -m600 /tmp/wifi_credentials.h \
    /opt/workstation/repo/main/ && rm /tmp/wifi_credentials.h'
```

It stays ignored, so it never reaches git history from CLAWBOX either.

**Firewall.** CLAWBOX runs `ufw`, and its rules were written when the only
client was the laptop - port 8000 was allowed from `192.168.1.65` alone, so
the device itself was silently dropped. Both ports are now open to the local
network:

```
8000/tcp  ALLOW  192.168.0.0/16   # WORKSTATION app API
8443/tcp  ALLOW  192.168.0.0/16   # WORKSTATION OTA
```

The symptom of getting this wrong is `ESP_ERR_HTTP_CONNECT` on the device
while the very same request succeeds from a laptop. Check `sudo ufw status`
before suspecting DNS.

## 9b. Why the TLS settings are what they are

Internal RAM, not flash, is the binding constraint on this board. mbedTLS
wants ~32KB of session buffers; after the UI is built there is about 20KB of
internal heap free, largest block ~7.6KB. The first OTA attempt failed with
`mbedtls_ssl_setup returned -0x7F00` (`MBEDTLS_ERR_SSL_ALLOC_FAILED`).

So `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` moves those buffers into the 16MB of
PSRAM that is otherwise idle, and the inbound record buffer is 8KB rather
than 16KB. nginx is configured with `ssl_buffer_size 4k` to match.

The OTA task's own stack stays at 6KB **in internal RAM**. It cannot move to
PSRAM: the task calls `esp_ota_write()`, which disables the flash cache, and
a stack in PSRAM would be unreachable mid-write. 8KB was tried and does not
fit in the largest free block.

If a future change makes internal RAM tighter still, the OTA task may fail to
start. That is logged and survivable - the device boots and works, it just
cannot update itself until a USB flash frees something up.
## 9c. Two failures worth knowing about

Both were found by running the thing, not by reading it, and both are the
same underlying fact: internal RAM is the constraint on this board.

**The hardware AES accelerator runs out of DMA memory on a long download.**
The first real wireless install died 593KB into a 2.6MB image with
`esp-aes: Failed to allocate memory`. The accelerator wants a DMA-capable
internal buffer for every record it decrypts; a short API call survives that,
a multi-megabyte TLS transfer does not. `CONFIG_MBEDTLS_HARDWARE_AES` is
therefore off. Software AES costs a second or two of CPU across the whole
image and asks for no DMA memory.

**The OTA task must be created before the UI.** It needs 6KB of contiguous
internal RAM. At the top of `app_main` that is easy; after LVGL, the audio
pipeline and the voice models have taken their share the largest free block
is about 7.6KB and `xTaskCreate` can simply fail. It did, on an image that
had just arrived over the air — which meant no health gate ran, the image
stayed `PENDING_VERIFY`, and the next reset rolled it back.

That was the rollback mechanism working exactly as intended, and it is a
useful thing to have seen happen for real. But it is also why
`ota_service_start()` is called early in `app_main` and the task then waits
on `ota_service_report_ui_ready()` before touching Wi-Fi or the backend
poller. **Anything added to `app_main` before that call is competing for the
memory the updater needs.**
## 10. Firmware identity

Set in the top-level `CMakeLists.txt`:

```
<semver>+<short git sha>[-dirty]        e.g. 1.1.0+cb93a81
```

`WORKSTATION_VERSION` is bumped by hand â€” it describes capability, which git
cannot infer. The SHA is read from the working tree at configure time.

The string is stamped into `esp_app_desc_t`, so it is visible:

- in the serial log at every boot (the identity block from `main.c`)
- on the device's FIRMWARE page
- in the manifest CLAWBOX publishes
- in each device check-in in the nginx access log

`workstation publish` reads the version back out of the compiled binary rather
than recomputing what it thinks it asked for, so the manifest cannot describe
an artifact it does not match. It refuses to publish a `-dirty` build unless
told `--allow-dirty`.

---

## 11. Troubleshooting

**The device is not taking a deploy.**
`workstation status` â€” if there are no recent check-ins, the device cannot
reach `:8443`. Check Wi-Fi, then whether `clawbox.local` resolves from the
device's network. The device's FIRMWARE page shows the last OTA message.

**"hash mismatch, not installing"** â€” the published artifact and its manifest
disagree. Republish. Nothing was installed; the running firmware is untouched.

**The device rebooted back to the old version.** The health gate failed. The
serial log names which condition (`wifi not online`, `backend not answering`,
â€¦). The previous firmware is running and safe; fix and deploy again.

**`nginx -t` fails after a certificate change.** The key and certificate must
be the matching pair, and nginx must be able to read both.

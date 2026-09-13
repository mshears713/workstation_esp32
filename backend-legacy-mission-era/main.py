"""Operation Homebound Mission 09/10 backend.

Minimal FastAPI service. Two independent capabilities live here, on purpose
kept from depending on each other:

  - Earthside Handshake (Mission 09): one health endpoint, one handshake
    endpoint the ESP32 POSTs accelerometer summaries to.
  - Audio capture upload (Mission 10): the ESP32 POSTs a completed
    microphone capture's raw PCM bytes straight to /api/v1/audio over the
    same Wi-Fi link the handshake uses (see main/audio_capture.c's
    upload_capture()) - no serial console involved. This endpoint wraps the
    bytes in a real WAV file under captures/; two more endpoints let you
    list/play those captures over HTTP.

No database, no auth, no HTTPS - this is a local development/learning
backend only, reachable over plain HTTP on the LAN (see _lan_ip below and
main/backend_config.h on the device side for the address the ESP32 actually
uses).

Run directly with the project's existing global Python install (already has
fastapi/uvicorn/pydantic - see README's "Run the backend" section):

    python backend/main.py
"""

from __future__ import annotations

import logging
import socket
import uuid
import wave
from datetime import datetime, timezone
from pathlib import Path
from typing import List, Optional

from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import FileResponse
from pydantic import BaseModel, Field

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("earthside_handshake")

app = FastAPI(title="Workstation Earthside Handshake", version="0.3.0")


class HandshakeRequest(BaseModel):
    device_id: str
    event_type: str
    mission: str
    device_uptime_ms: int
    sequence: Optional[int] = None
    sample_interval_ms: Optional[int] = None
    # Up to the device's last 10 accelerometer readings (magnitude, in g).
    # May be fewer than 10 (e.g. shortly after boot) or empty (no IMU) -
    # the device never pads this with fabricated values, so don't assume
    # exactly 10 here either.
    accel_samples_g: List[float] = Field(default_factory=list, max_length=10)


class HandshakeResponse(BaseModel):
    accepted: bool
    event_id: str
    server_time: str
    message: str
    accepted_sample_count: int


@app.get("/health")
def health() -> dict:
    return {"status": "ok"}


@app.post("/api/v1/handshake", response_model=HandshakeResponse)
def handshake(req: HandshakeRequest) -> HandshakeResponse:
    event_id = str(uuid.uuid4())
    server_time = datetime.now(timezone.utc).isoformat()
    n = len(req.accel_samples_g)

    if n:
        lo, hi = min(req.accel_samples_g), max(req.accel_samples_g)
        avg = sum(req.accel_samples_g) / n
        sample_summary = f"{n} samples {lo:.2f}g-{hi:.2f}g avg={avg:.2f}g"
    else:
        sample_summary = "0 samples"

    log.info(
        "handshake accepted: device_id=%s mission=%s seq=%s uptime_ms=%s %s -> event_id=%s",
        req.device_id, req.mission, req.sequence, req.device_uptime_ms, sample_summary, event_id,
    )

    return HandshakeResponse(
        accepted=True,
        event_id=event_id,
        server_time=server_time,
        message="Earthside link confirmed",
        accepted_sample_count=n,
    )


# ---- Audio capture upload (Mission 10) -------------------------------------
# Mirrors handshake's shape (device POSTs, backend accepts and answers) but
# the payload is raw PCM bytes, not JSON - metadata rides the query string
# instead, since a body that's already raw binary shouldn't also carry a
# JSON wrapper (that would mean base64-encoding it again, the exact
# overhead switching to Wi-Fi upload was meant to avoid).

CAPTURES_DIR = Path(__file__).resolve().parent.parent / "captures"


def _save_capture_wav(name: str, pcm_bytes: bytes, rate: int, bits: int, channels: int) -> Path:
    """`name` (e.g. "capture_003") comes from the device's in-RAM capture_seq,
    which resets to 0 on every reboot/reflash - so "capture_001" is not
    actually unique across the device's lifetime, only within one boot. The
    backend is the one place that can guarantee no collision, so it always
    stamps the save time into the filename rather than trusting the device's
    name to be unique - never silently overwrite a previous capture."""
    CAPTURES_DIR.mkdir(exist_ok=True)
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    out_path = CAPTURES_DIR / f"{name}_{timestamp}.wav"
    # Belt-and-suspenders for the (extremely unlikely) same-second collision -
    # still never overwrite, just disambiguate further.
    suffix = 2
    while out_path.exists():
        out_path = CAPTURES_DIR / f"{name}_{timestamp}_{suffix}.wav"
        suffix += 1

    with wave.open(str(out_path), "wb") as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(bits // 8)
        wav.setframerate(rate)
        wav.writeframes(pcm_bytes)
    return out_path


class AudioUploadResponse(BaseModel):
    accepted: bool
    saved_as: str
    bytes: int


@app.post("/api/v1/audio", response_model=AudioUploadResponse)
async def upload_audio(request: Request, name: str, rate: int, bits: int, ch: int) -> AudioUploadResponse:
    pcm = await request.body()
    out_path = _save_capture_wav(name, pcm, rate, bits, ch)
    log.info(
        "audio upload accepted: name=%s rate=%dHz bits=%d ch=%d -> %s (%d bytes)",
        name, rate, bits, ch, out_path, len(pcm),
    )
    return AudioUploadResponse(accepted=True, saved_as=out_path.name, bytes=len(pcm))


class CaptureInfo(BaseModel):
    name: str
    bytes: int
    modified: str


@app.get("/captures", response_model=List[CaptureInfo])
def list_captures() -> List[CaptureInfo]:
    if not CAPTURES_DIR.exists():
        return []
    files = sorted(CAPTURES_DIR.glob("*.wav"), key=lambda p: p.stat().st_mtime, reverse=True)
    return [
        CaptureInfo(
            name=f.name,
            bytes=f.stat().st_size,
            modified=datetime.fromtimestamp(f.stat().st_mtime, tz=timezone.utc).isoformat(),
        )
        for f in files
    ]


@app.get("/captures/{name}")
def get_capture(name: str) -> FileResponse:
    # Path(name).name strips any directory components (e.g. "../../secret")
    # before it ever touches the filesystem - name must resolve to a plain
    # filename inside CAPTURES_DIR, nothing else.
    safe_name = Path(name).name
    if not safe_name.endswith(".wav"):
        raise HTTPException(status_code=404, detail="not found")
    path = CAPTURES_DIR / safe_name
    if not path.is_file():
        raise HTTPException(status_code=404, detail="not found")
    return FileResponse(path, media_type="audio/wav", filename=safe_name)


def _lan_ip() -> str:
    """Best-effort LAN IP for a machine with multiple network adapters.

    Opens a UDP socket "connected" toward a public address - UDP connect
    never actually sends a packet, it just asks the OS routing table which
    local interface/IP it would use - the standard dependency-free trick
    for "what's my real LAN IP" (as opposed to a VPN/Docker/WSL virtual
    adapter that also happens to have an IPv4 address).
    """
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


if __name__ == "__main__":
    import uvicorn

    lan_ip = _lan_ip()
    port = 8000

    log.info("Earthside Handshake backend starting")
    log.info("Reachable from the ESP32 at: http://%s:%d", lan_ip, port)
    log.info("If that's not what's in main/backend_config.h, update BACKEND_BASE_URL there.")
    log.info("Auto-reload is on - saving a change to backend/*.py restarts the server automatically.")

    # 0.0.0.0, not 127.0.0.1: the ESP32 is a separate device on the LAN, not
    # this process's own loopback interface.
    #
    # reload=True needs the app passed as an import string ("main:app"), not
    # the `app` object directly - uvicorn's reloader runs the real server in
    # a subprocess and re-imports the module fresh on every change; it can't
    # do that with an object it was handed once at startup. reload_dirs is
    # scoped to just this file's own directory so unrelated changes
    # elsewhere in the repo (firmware source, captures/ output, the
    # ESP-IDF build/ tree) don't trigger a spurious restart.
    uvicorn.run(
        "main:app",
        host="0.0.0.0",
        port=port,
        reload=True,
        reload_dirs=[str(Path(__file__).resolve().parent)],
    )

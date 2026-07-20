"""Operation Homebound Mission 09 - Earthside Handshake backend.

Minimal FastAPI service proving the ESP32 <-> backend boundary: one health
endpoint, one handshake endpoint. No database, no auth, no HTTPS - this is a
local development/learning backend only, reachable over plain HTTP on the
LAN (see _lan_ip below and main/backend_config.h on the device side for the
address the ESP32 actually uses).

Run directly with the project's existing global Python install (already has
fastapi/uvicorn/pydantic - see README's "Run the backend" section):

    python backend/main.py
"""

from __future__ import annotations

import logging
import socket
import uuid
from datetime import datetime, timezone
from typing import List, Optional

from fastapi import FastAPI
from pydantic import BaseModel, Field

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("earthside_handshake")

app = FastAPI(title="Workstation Earthside Handshake", version="0.1.0")


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

    # 0.0.0.0, not 127.0.0.1: the ESP32 is a separate device on the LAN, not
    # this process's own loopback interface.
    uvicorn.run(app, host="0.0.0.0", port=port)

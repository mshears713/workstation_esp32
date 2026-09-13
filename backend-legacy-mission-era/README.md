# Dead code. This is not the backend that runs.

The folder is named this way on purpose. It used to be called `backend/`, which
made it look authoritative — an agent grepping for `/api/v1/` handlers found
this first and could reasonably conclude it was the live service. It is not,
and following it would mean deploying a stale single-file app over a working
one.

`main.py` here is the original single-file FastAPI service from the
mission era. It is kept for history, and because parts of the firmware's
endpoint contract are easiest to read against something this short.

**The backend WORKSTATION actually talks to is a separate repository:**

> https://github.com/mshears713/workstation-backend

It runs on CLAWBOX as `workstation-backend.service`, from
`/home/agent/.openclaw/workspace/workstation-backend`, and serves `:8000`. It
has routers for entries, issues, notes, notifications, projects, the remote
queue and the voice inbox, a LangGraph pipeline, and Notion/GitHub
integrations — none of which exist in the file next to this one.

If you are looking for the code behind `/api/v1/...`, it is over there. See
`doc/OTA.md` in this repo for how the two machines fit together.

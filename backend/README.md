# This is not the backend that runs

`main.py` here is the original single-file FastAPI service from the
mission-era build. It is kept for history and because parts of the firmware's
endpoint contract are easiest to read against it.

**The backend WORKSTATION actually talks to is a separate repository:**

> https://github.com/mshears713/workstation-backend

It runs on CLAWBOX as `workstation-backend.service`, from
`/home/agent/.openclaw/workspace/workstation-backend`, and serves `:8000`. It
has routers for entries, issues, notes, notifications, projects, the remote
queue and the voice inbox, a LangGraph pipeline, and Notion/GitHub
integrations — none of which exist in the file next to this one.

If you are looking for the code behind `/api/v1/...`, it is over there. See
`doc/OTA.md` in this repo for how the two machines fit together.

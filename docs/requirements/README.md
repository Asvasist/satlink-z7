# Requirements

The Software Requirements Specification is [satlink_srs.sdoc](satlink_srs.sdoc), written in
[StrictDoc](https://strictdoc.readthedocs.io) format (plain text, reviewable in pull requests).

```bash
pip install -r docs/requirements/requirements.txt   # inside the venv
strictdoc export docs/requirements --output-dir build/srs                          # HTML
strictdoc export docs/requirements --formats reqif-sdoc --output-dir build/srs-reqif # ReqIF for DOORS
strictdoc server docs/requirements                                                 # web editor
```

UID scheme: `SRS-<AREA>-<NNN>` with areas SYS (system), BLD (build and CI), ICD (interfaces),
LIB (libraries), BSP (board support), BOOT (boot chain), DOC (documentation).

Source files link to requirements with `@implements SRS-XXX-NNN` or `@verifies SRS-XXX-NNN` in a
comment. `python3 tools/trace/trace_matrix.py` generates the
[traceability matrix](../traceability/traceability.md); CI fails on unknown UIDs.

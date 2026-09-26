"""SatLink-Z7 ground segment in Python.

* :mod:`satlink.pus`     - CCSDS space packets with PUS-C headers
* :mod:`satlink.mission` - mission definitions, telecommand builders, report decoders
* :mod:`satlink.client`  - UDP client for satlink-payloadd (telecommands, telemetry)
* :mod:`satlink.scpi`    - the payload as a SCPI instrument (pyVISA or a plain socket)
"""

from .client import PayloadClient, TelecommandFailed
from .mission import Commander, interpret
from .scpi import ScpiInstrument

__all__ = ["Commander", "PayloadClient", "ScpiInstrument", "TelecommandFailed", "interpret"]
__version__ = "0.6.0"

"""Generate byte and semantic fixtures from the shared JSON."""

import json
from pathlib import Path
import sys

vectors = json.loads(Path(sys.argv[1]).read_text())
rows = []
packets = []
for v in vectors:
    data = ",".join(str(b) for b in bytes.fromhex(v["hex"]))
    rows.append("{{" + data + "}}")
    values = ",".join(str(float(v[k])) + "f" for k in ("vx", "vy", "wz"))
    packets.append(
        "{"
        + str(v["sequence"])
        + "U,static_cast<sakura::Action>("
        + str(v["action"])
        + "),{"
        + values
        + "}}"
    )
Path(sys.argv[2]).write_text(
    "constexpr std::array<std::array<uint8_t,20>,"
    + str(len(rows))
    + "> golden = {{"
    + ",".join(rows)
    + "}};\n"
    + "const std::array<sakura::Packet,"
    + str(len(rows))
    + "> expected = {{"
    + ",".join(packets)
    + "}};\n"
)

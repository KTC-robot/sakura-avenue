"""Generate service-packet byte fixtures (name -> bytes) from the shared JSON."""
import json
from pathlib import Path
import sys
vectors = json.loads(Path(sys.argv[1]).read_text())
items = []
for v in vectors:
    data = ",".join(str(b) for b in bytes.fromhex(v["hex"]))
    items.append('{"' + v["name"] + '",{' + data + "}}")
Path(sys.argv[2]).write_text(
    "const std::map<std::string, std::vector<uint8_t>> service_golden = {"
    + ",".join(items)
    + "};\n"
)

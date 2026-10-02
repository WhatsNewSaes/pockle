#!/usr/bin/env python3
"""Avoid Arduino 3.3.0's unsafe raw DNS-cache reset in this single-interface app."""
from pathlib import Path
p=Path(__file__).resolve().parent.parent/'.tools/data/packages/esp32/hardware/esp32/3.3.0/libraries/Network/src/NetworkManager.cpp'
s=p.read_text()
start=s.find('    // Pockle: serialize DNS cache changes.')
if start>=0:
    end=s.index('    log_d("Clearing DNS cache");',start)
    s=s[:start]+'    dns_clear_cache();\n'+s[end:]
if '    dns_clear_cache();' in s:
    s=s.replace('    dns_clear_cache();','    // Pockle: keep TTL-managed DNS cache; raw reset is unsafe here.')
s=s.replace('#include "lwip/priv/tcpip_priv.h"\n','')
p.write_text(s)

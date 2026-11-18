#!/usr/bin/env python3
"""Add per-IP request and connection limits to the hphp.ch playground routes."""
from datetime import datetime
from pathlib import Path
import shutil

CONFIG = Path("/etc/nginx/sites-enabled/hphp.ch")

def main() -> None:
    text = CONFIG.read_text()
    live = "    location /api/live/ {\n"
    api = "    location /api/ {\n"
    live_rules = (
        live
        + "        limit_req zone=hphp_live_requests burst=10 nodelay;\n"
        + "        limit_req zone=hphp_live_start burst=1 nodelay;\n"
        + "        limit_req_status 429;\n"
        + "        limit_conn hphp_live_connections 4;\n"
        + "        limit_conn_status 429;\n"
        + "        client_body_timeout 10s;\n"
    )
    api_rules = (
        api
        + "        limit_req zone=hphp_api burst=2 nodelay;\n"
        + "        limit_req_status 429;\n"
        + "        limit_conn hphp_api_connections 5;\n"
        + "        limit_conn_status 429;\n"
        + "        client_body_timeout 10s;\n"
    )
    if live_rules in text and api_rules in text:
        print("Playground limits already present")
        return
    if text.count(live) != 1 or text.count(api) != 1:
        raise SystemExit("Expected playground route blocks not found exactly once; config left unchanged")
    backup = CONFIG.with_name(CONFIG.name + ".bak-" + datetime.now().strftime("%Y%m%d-%H%M%S"))
    shutil.copy2(CONFIG, backup)
    text = text.replace(live, live_rules, 1).replace(api, api_rules, 1)
    CONFIG.write_text(text)
    print(f"Updated {CONFIG}; backup: {backup}")

if __name__ == "__main__":
    main()

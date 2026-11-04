#!/bin/bash
set -euo pipefail
BASE=/home/mshears/orlando-home-20261103
TARGET=/tmp/orlando-overnight-build
if [ ! -f "$TARGET/CMakeLists.txt" ]; then
 mkdir -p "$TARGET"
 sudo -n chown agent:agent "$TARGET"
 git -C "$BASE/fw-menu3" archive 19381ab14560b41ef2c55ec66bef992b75d768a1 > /tmp/orlando-overnight-base.tar
 chmod 644 /tmp/orlando-overnight-base.tar
 sudo -n -u agent tar -xf /tmp/orlando-overnight-base.tar -C "$TARGET"
 sudo -n -u agent cp -a /opt/workstation/repo/managed_components "$TARGET/managed_components"
 sudo -n -u agent ln -s /opt/workstation/repo/main/wifi_credentials.h "$TARGET/main/wifi_credentials.h"
fi
cd "$BASE/fw-overnight"
tar -cf /tmp/orlando-overnight-stage.tar main/CMakeLists.txt main/status_deck_ui.c main/ui_home.c main/ui_home.h main/ui_mouse.c main/ui_mouse.h main/orlando_v5_background.h main/orlando_v5_mouse.h main/orlando_castle_overnight.h main/orlando_sign_overnight.h main/orlando_princess_overnight.h main/orlando_van_door.h
chmod 644 /tmp/orlando-overnight-stage.tar
sudo -n -u agent tar -xf /tmp/orlando-overnight-stage.tar -C "$TARGET"
sudo -n -u agent timeout 900 bash -c 'cd /tmp/orlando-overnight-build && export IDF_TOOLS_PATH=/opt/workstation/.espressif IDF_PATH=/opt/workstation/esp-idf && . /opt/workstation/esp-idf/export.sh >/dev/null 2>&1 && nice -n 19 ionice -c3 idf.py reconfigure && nice -n 19 ionice -c3 ninja -C build -j2'
sha256sum "$TARGET/build/status_deck.bin"

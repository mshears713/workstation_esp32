/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Mission 09 local development backend address
 * @details The one place to change if the ESP32 needs to reach the backend
 *          at a different address - update BACKEND_BASE_URL and nothing
 *          else in handshake_client.c needs to change.
 *
 *          Must be your development laptop's LAN IP (e.g. 192.168.x.x),
 *          never 127.0.0.1 or "localhost" - those resolve to the ESP32
 *          itself, not your laptop. Run `ipconfig` (Windows) and use the
 *          IPv4 address of your Wi-Fi adapter - the same network the
 *          ESP32 joins via main/wifi_credentials.h. Update this whenever
 *          that address changes (new DHCP lease, different network).
 *
 *          Plain HTTP, no TLS - acceptable for this local learning mission
 *          only. Do not reuse this pattern for anything beyond the LAN.
 *
 *          Not git-ignored (unlike wifi_credentials.h): a LAN IP isn't a
 *          credential, and keeping it a normal tracked file is what makes
 *          it "change in one place" rather than "regenerate a local-only
 *          file after every clone."
 */
#pragma once

#define BACKEND_BASE_URL "http://192.168.1.31:8000"

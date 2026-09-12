idf_component_register(SRCS "main.c" "status_deck_ui.c" "wifi_manager.c" "handshake_client.c" "audio_capture.c" "voice_control.c" "note_client.c" "multipart_upload.c" "voice_inbox_client.c" "graph_client.c" "voice_listening_widget.c" "notification_client.c" "audio_playback.c" "entry_client.c" "project_selector.c" "ir_roku.c" "ir_nec_encoder.c" "remote_client.c" "stream_upload.c" "backend_health.c" "issue_client.c" "backend_catalog.c" "device_config.c" "firmware_identity.c" "ota_service.c"
                    INCLUDE_DIRS "."
                    # app_update: esp_ota_* and the rollback API (ota_service.c)
                    # esp-tls/mbedtls: the pinned-certificate HTTPS channel and
                    #   the SHA-256 used to check a downloaded image against the
                    #   hash CLAWBOX published
                    REQUIRES nvs_flash esp_wifi esp_netif esp_event esp_http_client json driver esp_driver_rmt app_update esp-tls mbedtls
                    # CLAWBOX's OTA certificate, linked in as a NUL-terminated
                    # string (ota_service.c refers to it as
                    # _binary_clawbox_ota_ca_pem_start). It is a public
                    # certificate, not a key - safe to commit, and it has to be
                    # in the image because there is nowhere else to trust from.
                    # Replacing CLAWBOX's certificate means rebuilding and
                    # deploying firmware; see doc/OTA.md.
                    EMBED_TXTFILES "certs/clawbox_ota_ca.pem")

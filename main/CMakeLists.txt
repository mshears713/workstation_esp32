idf_component_register(SRCS "main.c" "status_deck_ui.c" "wifi_manager.c" "handshake_client.c" "audio_capture.c" "voice_control.c" "note_client.c" "multipart_upload.c" "voice_inbox_client.c" "graph_client.c" "voice_listening_widget.c" "notification_client.c" "audio_playback.c" "entry_client.c" "project_selector.c" "ir_roku.c" "ir_nec_encoder.c" "remote_client.c" "stream_upload.c" "backend_health.c" "issue_client.c" "repo_selector.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash esp_wifi esp_netif esp_event esp_http_client json driver esp_driver_rmt)

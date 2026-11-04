# Artifacts & Instruments exhibit build (1.6.x). Retired from the link, kept in
# the tree and in git history: voice capture/commands (voice_control,
# audio_capture and the upload clients), recording/notification overlays,
# PROJECTS (project_selector, backend_catalog), survey, music, Tour B and its
# selector, weather, the IR/Roku remote, the legacy standalone mouse, the
# AUDIO TEST page and the dormant handshake. The exhibit's own modules are
# exhibit_audio (speaker owner), radar_presence (SENSOR dock AT581X) and
# presence_logic (when to welcome).
idf_component_register(SRCS "main.c" "status_deck_ui.c" "wifi_manager.c" "voice_listening_widget.c"
    "ui_kit.c" "ui_home.c" "ui_menu.c" "ui_firmware_page.c" "ui_diag_format.c" "ui_diagnostics.c"
    "ui_sensor_model.c" "ui_dial.c" "ui_sensors.c" "ui_log_format.c" "ui_log.c" "ui_settings.c" "ui_fw_timeline.c"
    "audio_mix.c" "audio_streams.c" "audio_test_session.c" "audio_playback.c" "exhibit_audio.c"
    "radar_presence.c" "presence_logic.c"
    "ui_adventure.c" "ui_story_dialogue.c"
    "newton_scene.c" "ui_newton.c" "newton_art.c" "newton_store.c"
    "lincoln_scene.c" "ui_lincoln.c" "lincoln_art.c" "lincoln_media.c"
    "backend_health.c" "device_config.c" "firmware_identity.c" "ota_service.c" "report_client.c" "diag.c"
    "lvgl_psram_alloc.c"
                    INCLUDE_DIRS "."
                    # app_update: esp_ota_* and the rollback API (ota_service.c)
                    # esp-tls/mbedtls: the pinned-certificate HTTPS channel and
                    #   the SHA-256 used to check a downloaded image against the
                    #   hash CLAWBOX published
                    REQUIRES nvs_flash esp_wifi esp_netif esp_event esp_http_client json driver esp_driver_i2c app_update esp-tls mbedtls
                    # CLAWBOX's OTA certificate, linked in as a NUL-terminated
                    # string (ota_service.c refers to it as
                    # _binary_clawbox_ota_ca_pem_start). It is a public
                    # certificate, not a key - safe to commit, and it has to be
                    # in the image because there is nowhere else to trust from.
                    # Replacing CLAWBOX's certificate means rebuilding and
                    # deploying firmware; see doc/OTA.md.
                    EMBED_TXTFILES "certs/clawbox_ota_ca.pem")

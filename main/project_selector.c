/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief The operator's routing hint - see project_selector.h.
 */

#include "project_selector.h"

#include "backend_catalog.h"

const char *project_selector_get_hint(void)
{
    const char *id = backend_catalog_project_id();
    /* backend_catalog returns "" when the list is empty or has not loaded.
     * The backend wants a value either way, and "none" is the one it already
     * understands as "the operator expressed no view" - which is exactly what
     * an empty catalog means for the purposes of the note. */
    return (id && id[0] != '\0') ? id : "none";
}

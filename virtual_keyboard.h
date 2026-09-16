#pragma once

#include <windows.h>

// Xbox 360 Guide-style virtual keyboard (async / overlapped).
// Returns ERROR_IO_PENDING on success. Completion is signaled on platform_handle->hEvent
// with platform_handle->Internal set to ERROR_SUCCESS or ERROR_CANCELLED.
// result_text receives the entered string (null-terminated, up to maximum_character_count-1).

unsigned long online_guide_show_virtual_keyboard_ui(
    int controller_index,
    unsigned long character_flags,
    wchar_t const* default_text,
    wchar_t const* title_text,
    wchar_t const* description_text,
    wchar_t* result_text,
    unsigned long maximum_character_count,
    OVERLAPPED* platform_handle);

/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AFTERLIGHT_FIDELITYFX_SDK_LINUX_H
#define AFTERLIGHT_FIDELITYFX_SDK_LINUX_H

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <locale>
#include <FidelityFX/host/ffx_util.h>

// FidelityFX 1.1.4 uses Microsoft CRT helpers in shared Vulkan code.
inline int wcscpy_s(wchar_t *dest, size_t capacity, const wchar_t *source)
{
    if (std::wcslen(source) >= capacity) {
        if (capacity) dest[0] = 0;
        return ERANGE;
    }
    std::wcscpy(dest, source);
    return 0;
}

template <size_t Capacity>
int wcscpy_s(wchar_t (&dest)[Capacity], const wchar_t *source)
{
    return wcscpy_s(dest, Capacity, source);
}

inline int strcpy_s(char *dest, size_t capacity, const char *source)
{
    if (std::strlen(source) >= capacity) {
        if (capacity) dest[0] = 0;
        return ERANGE;
    }
    std::strcpy(dest, source);
    return 0;
}

inline int wcstombs_s(size_t *written, char *dest, size_t capacity, const wchar_t *source, size_t limit)
{
    if (!capacity) return ERANGE;
    const size_t length = std::wcstombs(dest, source, limit < capacity ? limit : capacity - 1);
    if (length == static_cast<size_t>(-1)) {
        dest[0] = 0;
        *written = 0;
        return EILSEQ;
    }
    dest[length] = 0;
    *written = length + 1;
    return 0;
}

#define sprintf_s std::snprintf
#define _countof std::size

#endif

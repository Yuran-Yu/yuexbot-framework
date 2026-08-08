#pragma once
// XiaoLiZiVM_CppSDK compatibility header for YuexBot
// Plugins compiled against the original XiaoLiZiVM_CppSDK can be loaded by YuexBot.
// This header provides the x64-compatible SDK types and functions.
//
// For new plugins: use xlz_compat_sdk.h directly (it provides the same API).
// For existing XiaoLiZi plugins: recompile with this header, using the same
// apprun/RecviceGroupMesg/etc. entry points.
//
// Note: Original x86 XiaoLiZi DLLs cannot be loaded in x64 YuexBot.
// Plugins must be recompiled as x64 using this SDK.

#include "xlz_compat_sdk.h"

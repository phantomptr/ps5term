// PS5Term - reproducible build identity. GPL-3.0-or-later.
#pragma once
#ifdef PS5TERM_GENERATED_BUILD_INFO
#include "build_identity.hpp"
#else
#define PS5TERM_BUILD_ID "unidentified-development-build"
#endif
#ifndef PS5TERM_VERSION
#define PS5TERM_VERSION "development"
#endif
namespace term
{
inline constexpr char build_id[] = PS5TERM_BUILD_ID;
inline constexpr char version[] = PS5TERM_VERSION; // sce_sys/param.json contentVersion
} // namespace term

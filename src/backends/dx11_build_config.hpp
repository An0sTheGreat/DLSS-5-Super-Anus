#pragma once
// The game-test build uses the proven cleanup paths, not synthetic feature-
// release probes. Ordinary builds keep their existing conservative behavior.
#if defined(NR_DX11_GAME_TEST)
#if !defined(NR_EXPERIMENTAL_DX11)
#error DX11 game testing requires the native DX11 adapter
#endif
#if defined(NR_DX11_RECYCLE_PROBE) || defined(NR_DX11_REPLACEMENT_PROBE) || defined(NR_DX11_CORE_SHUTDOWN_PROBE) || defined(NR_DX11_CORE_RECREATE_PROBE)
#error Do not combine game testing with synthetic lifecycle probes
#endif
#define NR_DX11_CORE_SHUTDOWN_ENABLED
#define NR_DX11_CORE_RECREATE_ENABLED
#define NR_DX11_LIFECYCLE_PREFIX "DX11 game test: "
#else
#ifdef NR_DX11_CORE_SHUTDOWN_PROBE
#define NR_DX11_CORE_SHUTDOWN_ENABLED
#endif
#ifdef NR_DX11_CORE_RECREATE_PROBE
#define NR_DX11_CORE_RECREATE_ENABLED
#endif
#define NR_DX11_LIFECYCLE_PREFIX "TEST ONLY: "
#endif

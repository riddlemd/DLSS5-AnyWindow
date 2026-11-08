#pragma once

namespace Magpie {

// Loads sdli1995's dlssg_sm86 unlock (as packaged by DLSS-Unlocked) into this process so
// NGX DLSS Frame Generation runs on RTX 20 (SM75) and RTX 30 (SM8x) GPUs.
//
// Layout beside the executable (copied from DLSS-Unlocked's OptiScaler\dlssg_sm86 folder):
//   dlssg_sm86\dlssg_sm86.dll       the unlock module (a version.dll-style proxy)
//   dlssg_sm86\dlssg_sm86.ini       its settings
//   dlssg_sm86\dlssg_sm86\...       backend, kernels and bundled nvngx_dlssg runtime
//
// MAGPIE_DLSSG_SM86 selects the mode: "auto" (default; load on compute capability 7.5-8.7),
// "on" (load on any NVIDIA GPU) or "off".
struct DLSSGSm86UnlockStatus {
	bool attempted = false;
	bool loaded = false;
	int computeMajor = 0;
	int computeMinor = 0;
	std::string detail;
};

// Must run before the first NGX initialization in the process: the module only takes over
// NGX components that load after it. Loads at most once per process; later calls return the
// first result.
const DLSSGSm86UnlockStatus& EnsureDLSSGSm86Unlock(IDXGIAdapter* adapter) noexcept;

}

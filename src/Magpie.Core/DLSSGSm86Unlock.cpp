#include "pch.h"
#include "DLSSGSm86Unlock.h"
#include "Logger.h"
#include "StrHelper.h"
#include <fstream>
#include <mutex>

namespace Magpie {

namespace {

enum class UnlockMode { Auto, On, Off };

UnlockMode ReadMode() noexcept {
	wchar_t value[16]{};
	const DWORD length = GetEnvironmentVariableW(L"MAGPIE_DLSSG_SM86", value, 16);
	if (length == 0 || length >= 16) {
		return UnlockMode::Auto;
	}
	if (_wcsicmp(value, L"off") == 0 || _wcsicmp(value, L"0") == 0) {
		return UnlockMode::Off;
	}
	if (_wcsicmp(value, L"on") == 0 || _wcsicmp(value, L"1") == 0) {
		return UnlockMode::On;
	}
	return UnlockMode::Auto;
}

// The adapter name alone cannot separate workstation Turing/Ampere from Ada, so ask CUDA
// for the compute capability of the exact adapter (matched by LUID).
bool QueryComputeCapability(LUID adapterLuid, int& major, int& minor) noexcept {
	using CuInitFn = int(__stdcall*)(unsigned int);
	using CuDeviceGetCountFn = int(__stdcall*)(int*);
	using CuDeviceGetFn = int(__stdcall*)(int*, int);
	using CuDeviceGetLuidFn = int(__stdcall*)(char*, unsigned int*, int);
	using CuDeviceGetAttributeFn = int(__stdcall*)(int*, int, int);

	HMODULE module = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!module) {
		return false;
	}
	auto unload = wil::scope_exit([&]() { FreeLibrary(module); });

	const auto cuInit = reinterpret_cast<CuInitFn>(GetProcAddress(module, "cuInit"));
	const auto cuDeviceGetCount =
		reinterpret_cast<CuDeviceGetCountFn>(GetProcAddress(module, "cuDeviceGetCount"));
	const auto cuDeviceGet = reinterpret_cast<CuDeviceGetFn>(GetProcAddress(module, "cuDeviceGet"));
	const auto cuDeviceGetLuid =
		reinterpret_cast<CuDeviceGetLuidFn>(GetProcAddress(module, "cuDeviceGetLuid"));
	const auto cuDeviceGetAttribute =
		reinterpret_cast<CuDeviceGetAttributeFn>(GetProcAddress(module, "cuDeviceGetAttribute"));
	if (!cuInit || !cuDeviceGetCount || !cuDeviceGet || !cuDeviceGetLuid ||
		!cuDeviceGetAttribute || cuInit(0) != 0) {
		return false;
	}

	int count = 0;
	if (cuDeviceGetCount(&count) != 0) {
		return false;
	}
	for (int ordinal = 0; ordinal < count; ++ordinal) {
		int device = 0;
		char luid[sizeof(LUID)]{};
		unsigned int nodeMask = 0;
		if (cuDeviceGet(&device, ordinal) != 0 ||
			cuDeviceGetLuid(luid, &nodeMask, device) != 0 ||
			std::memcmp(luid, &adapterLuid, sizeof(LUID)) != 0) {
			continue;
		}
		// CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR / _MINOR
		return cuDeviceGetAttribute(&major, 75, device) == 0 &&
			cuDeviceGetAttribute(&minor, 76, device) == 0;
	}
	return false;
}

std::filesystem::path ExecutableDirectory() {
	std::wstring path(MAX_PATH, L'\0');
	for (;;) {
		const DWORD length = GetModuleFileNameW(nullptr, path.data(), (DWORD)path.size());
		if (length == 0) {
			return {};
		}
		if (length < path.size()) {
			path.resize(length);
			break;
		}
		path.resize(path.size() * 2);
	}
	return std::filesystem::path(path).parent_path();
}

// Used only when the packaged dlssg_sm86.ini is missing; mirrors the factory defaults.
constexpr const char* DEFAULT_INI =
	"; Written by Magpie because no dlssg_sm86.ini was packaged.\n"
	"[FrameGeneration]\n"
	"MaxGeneratedFrames=3\n"
	"Engine=DLSS\n\n"
	"[DLSSG]\n"
	"Optimized=2\n"
	"Preset=Auto\n\n"
	"[Logging]\n"
	"Level=1\n"
	"Directory=dlssg_sm86\\logs\n\n"
	"[Runtime]\n"
	"Mode=Bundled\n";

DLSSGSm86UnlockStatus Load(IDXGIAdapter* adapter) noexcept {
	DLSSGSm86UnlockStatus status;
	status.attempted = true;

	const UnlockMode mode = ReadMode();
	if (mode == UnlockMode::Off) {
		status.detail = "disabled by MAGPIE_DLSSG_SM86=off";
		return status;
	}

	DXGI_ADAPTER_DESC desc{};
	if (!adapter || FAILED(adapter->GetDesc(&desc)) || desc.VendorId != 0x10de) {
		status.detail = "adapter is not NVIDIA";
		return status;
	}

	const bool haveCapability =
		QueryComputeCapability(desc.AdapterLuid, status.computeMajor, status.computeMinor);
	if (mode == UnlockMode::Auto) {
		if (!haveCapability) {
			status.detail = "compute capability unknown; set MAGPIE_DLSSG_SM86=on to force";
			return status;
		}
		// Turing is 7.5, Ampere 8.0-8.7; Ada (8.9) and newer run DLSS-G natively.
		const int cc = status.computeMajor * 10 + status.computeMinor;
		if (cc < 75 || cc > 87) {
			status.detail = fmt::format(
				"not needed on compute capability {}.{}", status.computeMajor, status.computeMinor);
			return status;
		}
	}

	std::error_code ec;
	const std::filesystem::path dir = ExecutableDirectory() / L"dlssg_sm86";
	const std::filesystem::path dll = dir / L"dlssg_sm86.dll";
	if (!std::filesystem::is_regular_file(dll, ec)) {
		status.detail = "dlssg_sm86\\dlssg_sm86.dll not found beside the executable";
		return status;
	}
	if (!std::filesystem::is_directory(dir / L"dlssg_sm86", ec)) {
		// The proxy alone aborts with backend_unusable; the backend folder is mandatory.
		status.detail = "dlssg_sm86\\dlssg_sm86\\ backend folder is missing";
		return status;
	}
	const std::filesystem::path ini = dir / L"dlssg_sm86.ini";
	if (!std::filesystem::is_regular_file(ini, ec)) {
		std::ofstream(ini, std::ios::binary) << DEFAULT_INI;
	}

	HMODULE module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!module) {
		status.detail = fmt::format("LoadLibrary failed ({})", GetLastError());
		return status;
	}
	status.loaded = true;
	status.detail = StrHelper::UTF16ToUTF8(dll.native());
	return status;
}

}

const DLSSGSm86UnlockStatus& EnsureDLSSGSm86Unlock(IDXGIAdapter* adapter) noexcept {
	static DLSSGSm86UnlockStatus status;
	static std::once_flag once;
	std::call_once(once, [&]() {
		status = Load(adapter);
		if (status.loaded) {
			Logger::Get().Info(fmt::format(
				"dlssg_sm86 unlock loaded for compute capability {}.{}: {}",
				status.computeMajor, status.computeMinor, status.detail));
		} else {
			Logger::Get().Info("dlssg_sm86 unlock not loaded: " + status.detail);
		}
	});
	return status;
}

}

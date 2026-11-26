// Manual integration regression: run beside the add-on with the path to an
// add-on-enabled ReShade DLL. No game, saved configuration or injection required.
#include <windows.h>
#include <d3d11.h>
#include <cstdio>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::fputs("Usage: probe.exe <absolute path to ReShade DLL>\n", stderr);
        return 2;
    }
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    SetEnvironmentVariableW(L"RESHADE_DISABLE_LOADING_CHECK", L"1");
    if (!LoadLibraryW(argv[1])) {
        std::fprintf(stderr, "ReShade load failed: %lu\n", GetLastError());
        return 3;
    }
    const auto dll = LoadLibraryW(L"d3d11.dll");
    const auto create = dll ? reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(dll, "D3D11CreateDevice")) : nullptr;
    if (!create)
        return 4;

    ID3D11Device* devices[2]{};
    ID3D11DeviceContext* contexts[2]{};
    ID3D11Texture2D* textures[2]{};
    for (int i = 0; i < 2; ++i) {
        HRESULT result = create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                &devices[i], nullptr, &contexts[i]);
        if (FAILED(result)) {
            std::fprintf(stderr, "Device %d creation failed: %08lx\n", i, result);
            return 5;
        }
        if (!GetModuleHandleW(sizeof(void*) == 8 ? L"ReshadeEffectShaderToggler.addon64" : L"ReshadeEffectShaderToggler.addon32")) {
            std::fputs("REST was not loaded; cannot test its callbacks.\n", stderr);
            return 6;
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = 64;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        result = devices[i]->CreateTexture2D(&desc, nullptr, &textures[i]);
        if (FAILED(result)) {
            std::fprintf(stderr, "Texture %d creation failed: %08lx\n", i, result);
            return 7;
        }
    }
    // Keep device 0 alive so REST remains loaded when device 1's late callback
    // runs. This reproduced the original onDestroyResource / _Mtx_lock crash.
    for (int i = 1; i >= 0; --i) {
        contexts[i]->Release();
        devices[i]->Release();
        std::printf("Release texture %d after device proxy teardown\n", i);
        std::fflush(stdout);
        textures[i]->Release();
    }
    std::puts("PASS");
    return 0;
}

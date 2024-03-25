#define STRICT
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define _USE_MATH_DEFINES

#include <Windows.h>
#include <tchar.h>

#include <avrt.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl.h>

#include <filesystem>

#include <obs-module.h>
#include <util/platform.h>
#include <media-io/audio-math.h>

#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "obs.lib")
#pragma comment(lib, "w32-pthreads.lib")

namespace {
#define OBS_DEBUG(FORMAT, ...) obs_log(LOG_DEBUG  , "[nair-rtvc-source] " FORMAT, ##__VA_ARGS__)
#define OBS_INFO(FORMAT, ...)  obs_log(LOG_INFO   , "[nair-rtvc-source] " FORMAT, ##__VA_ARGS__)
#define OBS_WARN(FORMAT, ...)  obs_log(LOG_WARNING, "[nair-rtvc-source] " FORMAT, ##__VA_ARGS__)
#define OBS_ERROR(FORMAT, ...) obs_log(LOG_ERROR  , "[nair-rtvc-source] " FORMAT, ##__VA_ARGS__)

  void obs_log(int log_level, char const* format, ...)
  {
    va_list args;
    va_start(args, format);
    blogva(log_level, format, args);
    va_end(args);
  }

  constexpr TCHAR const VVFX_FILE[] = L"VVFX\\rtvc.vvfx";

  typedef int(__stdcall* rtvc_get_protocol_version_fn)(int* major_version, int* minor_version, int* revision);
  typedef int(__stdcall* rtvc_init_fn)(char const* model_name);
  typedef int(__stdcall* rtvc_destroy_fn)();
  typedef int(__stdcall* rtvc_process_fn)(int num_params, float const* params, float const* x, float* y);

  typedef int(__stdcall* rtvc_get_version_fn)(int* major_version, int* minor_version, int* revision);
  typedef int(__stdcall* rtvc_get_sample_rate_fn)(int* sample_rate);
  typedef int(__stdcall* rtvc_get_block_size_fn)(int* block_size);
  typedef int(__stdcall* rtvc_get_sample_latency_fn)(int* sample_latency);

  typedef int(__stdcall* rtvc_get_num_params_fn)(int* num_params);
  typedef int(__stdcall* rtvc_get_param_name_fn)(int i, char const** param_name);

  typedef int(__stdcall* rtvc_get_num_voices_fn)(int* num_voices);
  typedef int(__stdcall* rtvc_get_voice_name_fn)(int i, char const** voice_name);
  typedef int(__stdcall* rtvc_set_voice_fn)(int voice_id);
  typedef int(__stdcall* rtvc_set_voices_fn)(int num_voices, int const* voice_ids, float const* voice_amounts);

  rtvc_get_protocol_version_fn  rtvc_get_protocol_version;
  rtvc_init_fn                  rtvc_init;
  rtvc_destroy_fn               rtvc_destroy;
  rtvc_process_fn               rtvc_process;

  rtvc_get_version_fn           rtvc_get_version;
  rtvc_get_sample_rate_fn       rtvc_get_sample_rate;
  rtvc_get_sample_latency_fn    rtvc_get_sample_latency;
  rtvc_get_block_size_fn        rtvc_get_block_size;

  rtvc_get_num_params_fn        rtvc_get_num_params;
  rtvc_get_param_name_fn        rtvc_get_param_name;

  rtvc_get_num_voices_fn        rtvc_get_num_voices;
  rtvc_get_voice_name_fn        rtvc_get_voice_name;
  rtvc_set_voice_fn             rtvc_set_voice;
  rtvc_set_voices_fn            rtvc_set_voices;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
  static HMODULE hDynamicModule = nullptr;

  switch (ul_reason_for_call)
  {
  case DLL_PROCESS_ATTACH:
  {
    std::wstring library_path(L"C:\\Program Files\\Common Files");
    std::vector<TCHAR> buf(GetEnvironmentVariable(L"CommonProgramFiles", nullptr, 0));
    if (GetEnvironmentVariable(L"CommonProgramFiles", buf.data(), static_cast<DWORD>(buf.size()))) {
      library_path = buf.data();
    }
    library_path += L"\\";
    library_path += VVFX_FILE;
    hDynamicModule = LoadLibrary(library_path.c_str());
    if (!hDynamicModule) {
      TCHAR szModulePath[MAX_PATH];
      ::GetModuleFileName(hModule, szModulePath, static_cast<DWORD>(std::size(szModulePath)));

      std::filesystem::path local_library_path{ szModulePath };
      local_library_path = local_library_path.parent_path() / VVFX_FILE;

      hDynamicModule = ::LoadLibrary(local_library_path.c_str());
      if (!hDynamicModule) {
        return FALSE;
      }
    }

    // protocol
    {
      rtvc_get_protocol_version = reinterpret_cast<rtvc_get_protocol_version_fn>(::GetProcAddress(hDynamicModule, "get_protocol_version"));
      if (!rtvc_get_protocol_version) {
        return FALSE;
      }

      rtvc_init = reinterpret_cast<rtvc_init_fn>(::GetProcAddress(hDynamicModule, "init"));
      if (!rtvc_init) {
        return FALSE;
      }

      rtvc_destroy = reinterpret_cast<rtvc_destroy_fn>(::GetProcAddress(hDynamicModule, "destroy"));
      if (!rtvc_destroy) {
        return FALSE;
      }

      rtvc_process = reinterpret_cast<rtvc_process_fn>(::GetProcAddress(hDynamicModule, "process"));
      if (!rtvc_process) {
        return FALSE;
      }
    }

    // model
    {
      rtvc_get_version = reinterpret_cast<rtvc_get_version_fn>(::GetProcAddress(hDynamicModule, "get_version"));
      if (!rtvc_get_version) {
        return FALSE;
      }

      rtvc_get_sample_rate = reinterpret_cast<rtvc_get_sample_rate_fn>(::GetProcAddress(hDynamicModule, "get_sample_rate"));
      if (!rtvc_get_sample_rate) {
        return FALSE;
      }

      rtvc_get_sample_latency = reinterpret_cast<rtvc_get_sample_latency_fn>(::GetProcAddress(hDynamicModule, "get_sample_latency"));
      if (!rtvc_get_sample_latency) {
        return FALSE;
      }

      rtvc_get_block_size = reinterpret_cast<rtvc_get_block_size_fn>(::GetProcAddress(hDynamicModule, "get_block_size"));
      if (!rtvc_get_block_size) {
        return FALSE;
      }
    }

    // param
    {
      rtvc_get_num_params = reinterpret_cast<rtvc_get_num_params_fn>(::GetProcAddress(hDynamicModule, "get_num_params"));
      if (!rtvc_get_num_params) {
        return FALSE;
      }

      rtvc_get_param_name = reinterpret_cast<rtvc_get_param_name_fn>(::GetProcAddress(hDynamicModule, "get_param_name"));
      if (!rtvc_get_param_name) {
        return FALSE;
      }
    }

    // voice
    {
      rtvc_get_num_voices = reinterpret_cast<rtvc_get_num_voices_fn>(::GetProcAddress(hDynamicModule, "get_num_voices"));
      if (!rtvc_get_num_voices) {
        return FALSE;
      }

      rtvc_get_voice_name = reinterpret_cast<rtvc_get_voice_name_fn>(::GetProcAddress(hDynamicModule, "get_voice_name"));
      if (!rtvc_get_voice_name) {
        return FALSE;
      }

      rtvc_set_voice = reinterpret_cast<rtvc_set_voice_fn>(::GetProcAddress(hDynamicModule, "set_voice"));
      if (!rtvc_set_voice) {
        return FALSE;
      }

      rtvc_set_voices = reinterpret_cast<rtvc_set_voices_fn>(::GetProcAddress(hDynamicModule, "set_voices"));
      if (!rtvc_set_voices) {
        return FALSE;
      }
    }

    break;
  }
  case DLL_THREAD_ATTACH:
  case DLL_THREAD_DETACH:
    break;
  case DLL_PROCESS_DETACH:
    if (hDynamicModule) {
      FreeLibrary(hDynamicModule);
      hDynamicModule = nullptr;
    }
    break;
  }
  return TRUE;
}

namespace {
  class OBSAudioSource final {
    static constexpr char const* const LATENCY_MODES[] = {
      "minimum-latency",
      "ultra_low-latency",
      "hyper_low-latency",
      "super_low-latency",
      "very_low-latency",
      "low-latency",
      "middle-latency",
      "high-latency",
      "very_high-latency",
      "super_high-latency",
      "hyper_high-latency",
      "ultra_high-latency",
      "maximum-latency",
    };

  public:
    OBSAudioSource(obs_source_t* context)
      : context_(context)
    {
    }

    // 構築する
    HRESULT Init() {
      HRESULT hr = S_OK;

      Microsoft::WRL::ComPtr<IMMDeviceEnumerator> pDeviceEnumerator;
      if FAILED(hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDeviceEnumerator)))
      {
        std::string const& msg = std::system_category().message(hr);
        OBS_ERROR("unable to instantiate pDeviceIn enumerator: %s (%x)", msg.c_str(), hr);
        return hr;
      }

      if FAILED(hr = pDeviceEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pDeviceCollection_))
      {
        std::string const& msg = std::system_category().message(hr);
        OBS_ERROR("unable to retrieve pDeviceIn collection: %s (%x)", msg.c_str(), hr);
        return hr;
      }

      {
        int major_version = -1;
        int minor_version = -1;
        int revision = -1;
        if (int const retval = rtvc_get_protocol_version(&major_version, &minor_version, &revision)) {
          OBS_ERROR("could not get protocol version: %d", retval);
          return E_FAIL;
        }
        OBS_INFO("protocol version: %d.%d.%d", major_version, minor_version, revision);

        if (major_version != 1) {
          OBS_ERROR("unsupported protocol version: %d.%d.%d", major_version, minor_version, revision);
          return E_FAIL;
        }

        if (int const retval = rtvc_init("jvs100")) {
          OBS_ERROR("could not init jvs100: %d", retval);
          return E_FAIL;
        }
        OBS_INFO("init: jvs100");
      }
      {
        int major_version = -1;
        int minor_version = -1;
        int revision = -1;
        if (int const retval = rtvc_get_version(&major_version, &minor_version, &revision)) {
          OBS_ERROR("could not get version: %d", retval);
          return E_FAIL;
        }
        OBS_INFO("version: %d.%d.%d", major_version, minor_version, revision);

        if (int const retval = rtvc_get_sample_rate(&sample_rate_)) {
          OBS_ERROR("could not get sample rate: %d", retval);
          return E_FAIL;
        }
        OBS_INFO("sample rate: %d [hz]", sample_rate_);

        int sample_latency = 0;
        if (int const retval = rtvc_get_sample_latency(&sample_latency)) {
          OBS_ERROR("could not get sample latency: %d", retval);
          return E_FAIL;
        }
        OBS_INFO("sample latency: %d [ms]", 1'000 * sample_latency / sample_rate_);

        if (int const retval = rtvc_get_block_size(&block_size_)) {
          OBS_ERROR("could not get block size: %d", retval);
          return E_FAIL;
        }
        OBS_INFO("block size: %d [ms]", 1'000 * block_size_ / sample_rate_);
      }

      return S_OK;
    }

    // 破棄する
    HRESULT Destroy() {
      HRESULT hr = S_OK;

      if FAILED(hr = Stop()) {
        return hr;
      }

      if (int const retval = rtvc_destroy()) {
        OBS_ERROR("Could not destroy RTVC Engine: %d", retval);
        return E_FAIL;
      }

      return hr;
    }

    // パラメーターを定義する
    HRESULT GetProperties(obs_properties_t& props) {
      HRESULT hr = S_OK;
      {
        obs_property_t* prop_device = obs_properties_add_list(&props, "device", "Input", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

        UINT deviceCount = 0;
        if FAILED(hr = pDeviceCollection_->GetCount(&deviceCount)) {
          std::string const& msg = std::system_category().message(hr);
          OBS_ERROR("Unable to get pDeviceIn collection length: %s (%x)", msg.c_str(), hr);
          return hr;
        }

        device_count_ = static_cast<int>(deviceCount);

        for (UINT i = 0; i < deviceCount; ++i) {
          Microsoft::WRL::ComPtr<IMMDevice> pDevice;
          if FAILED(hr = pDeviceCollection_->Item(i, &pDevice)) {
            std::string const& msg = std::system_category().message(hr);
            OBS_WARN("Unable to open pDeviceIn %d property store: %s (%x)", i, msg.c_str(), hr);
            continue;
          }

          Microsoft::WRL::ComPtr<IPropertyStore> propertyStore;
          if FAILED(hr = pDevice->OpenPropertyStore(STGM_READ, &propertyStore)) {
            std::string const& msg = std::system_category().message(hr);
            OBS_WARN("Unable to open pDeviceIn %d property store: %s (%x)", i, msg.c_str(), hr);
            continue;
          }

          PROPVARIANT pv;
          ::PropVariantInit(&pv);
          if FAILED(hr = propertyStore->GetValue(PKEY_Device_FriendlyName, &pv))
          {
            ::PropVariantClear(&pv);
            std::string const& msg = std::system_category().message(hr);
            OBS_WARN("Unable to retrieve friendly name for pDeviceIn (%d): %s (%x)", i, msg.c_str(), hr);
            continue;
          }

          if (pv.vt != VT_LPWSTR) {
            ::PropVariantClear(&pv);
            continue;
          }

          std::string device_name(::WideCharToMultiByte(CP_UTF8, WC_COMPOSITECHECK, pv.pwszVal, -1, nullptr, 0, nullptr, nullptr), '\0');
          if (!::WideCharToMultiByte(CP_UTF8, WC_COMPOSITECHECK, pv.pwszVal, -1, device_name.data(), static_cast<int>(device_name.size()), nullptr, nullptr)) {
            ::PropVariantClear(&pv);
            continue;
          }
          ::PropVariantClear(&pv);

          obs_property_list_add_int(prop_device, device_name.c_str(), i);
        }
      }
      {
        obs_property_t* prop_latency = obs_properties_add_list(&props, "latency", "Latency", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
        for (std::size_t i = 0, size = static_cast<int>(std::size(LATENCY_MODES)); i < size; ++i) {
          obs_property_list_add_int(prop_latency, LATENCY_MODES[i], i+1);
        }
      }
      {
        obs_property_t* prop_input_gain = obs_properties_add_float_slider(&props, "input_gain", "Input Gain", -6, 6, 0.01);
        obs_property_float_set_suffix(prop_input_gain, " db");
      }
      {
        obs_property_t* prop_output_gain = obs_properties_add_float_slider(&props, "output_gain", "Output Gain", -6, 6, 0.01);
        obs_property_float_set_suffix(prop_output_gain, " db");
      }
      {
        obs_property_t* prop_pitch_shift = obs_properties_add_float_slider(&props, "pitch_shift", "Pitch Shift", -1200, 1200, 1);
        obs_property_float_set_suffix(prop_pitch_shift, " cent");
      }
      {
        obs_property_t* prop_pitch_shift_mode = obs_properties_add_list(&props, "pitch_shift_mode", "Pitch Shift Mode", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
        obs_property_list_add_int(prop_pitch_shift_mode, "song", 0);
        obs_property_list_add_int(prop_pitch_shift_mode, "talk", 1);
      }
      {
        obs_property_t* prop_pitch_snap = obs_properties_add_float_slider(&props, "pitch_snap", "Pitch Snap", 0, 100, 1);
        obs_property_float_set_suffix(prop_pitch_snap, " %");
      }
      {
        int num_voices = 0;
        if (int retval = rtvc_get_num_voices(&num_voices)) {
          return retval;
        }
        obs_property_t* prop_primary_voice = obs_properties_add_list(&props, "primary_voice", "Primary Voice", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
        obs_property_t* prop_secondary_voice = obs_properties_add_list(&props, "secondary_voice", "Secondary Voice", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

        obs_property_list_add_int(prop_secondary_voice, "none", -1);
        for (int i = 0; i < num_voices; ++i) {
          char const* voice_name = nullptr;
          if (int const retval = rtvc_get_voice_name(i, &voice_name)) {
          }
          else {
            obs_property_list_add_int(prop_primary_voice, voice_name, i);
            obs_property_list_add_int(prop_secondary_voice, voice_name, i);
          }
        }
      }
      {
        obs_property_t* prop_amount = obs_properties_add_float_slider(&props, "amount", "Amount", 0, 100, 1);
        obs_property_float_set_suffix(prop_amount, " %");
      }
      return hr;
    }

    HRESULT Start() {
      HRESULT hr = S_OK;

      if (hEvtAudioCaptureSamplesReady_) {
        ::CloseHandle(hEvtAudioCaptureSamplesReady_);
        hEvtAudioCaptureSamplesReady_ = nullptr;
      }
      hEvtAudioCaptureSamplesReady_ = ::CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
      if (!hEvtAudioCaptureSamplesReady_) {
        hr = ::GetLastError();
        std::string const& msg = std::system_category().message(hr);
        OBS_ERROR("unable to create samples ready event: %s (%x)", msg.c_str(), hr);
        return hr;
      }

      if (hEvtShutdown_) {
        ::CloseHandle(hEvtShutdown_);
        hEvtShutdown_ = nullptr;
      }
      hEvtShutdown_ = ::CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
      if (!hEvtShutdown_) {
        hr = ::GetLastError();
        std::string const& msg = std::system_category().message(hr);
        OBS_ERROR("unable to create shutdown event: %s (%x)", msg.c_str(), hr);
        return hr;
      }

      int const device_id = device_id_.load(std::memory_order::acquire);
      int const latency_type = latency_mode_.load(std::memory_order::acquire);
      if FAILED(hr = pDeviceCollection_->Item(device_id, &pDevice_)) {
        std::string const& msg = std::system_category().message(hr);
        OBS_ERROR("unable to retrieve pDeviceIn: %s (%x)", msg.c_str(), hr);
        return hr;
      }

      // AudioClient準備
      if FAILED(hr = pDevice_->Activate(__uuidof(IAudioClient3), CLSCTX_INPROC_SERVER, nullptr, &pAudioClientIn_)) {
        std::string const& msg = std::system_category().message(hr);
        OBS_ERROR("unable to activate audio pAudioClientIn: %s (%x)", msg.c_str(), hr);
        return hr;
      }

      // デバイスのレイテンシ取得
      REFERENCE_TIME hnsDefaultDevicePeriod;
      REFERENCE_TIME hnsMinimumDevicePeriod;
      if FAILED(hr = pAudioClientIn_->GetDevicePeriod(&hnsDefaultDevicePeriod, &hnsMinimumDevicePeriod)) {
        std::string const& msg = std::system_category().message(hr);
        OBS_ERROR("unable to get pDeviceIn period: %s (%x)", msg.c_str(), hr);
        return hr;
      }

      OBS_INFO("default device period: %ld [ms]", hnsDefaultDevicePeriod / 10'000);
      OBS_INFO("minimum device period: %ld [ms]", hnsMinimumDevicePeriod / 10'000);

      int const SAMPLE_RATE = sample_rate_;
      int const BLOCK_SIZE = block_size_;

      REFERENCE_TIME const hnsBufferPeriod = hnsDefaultDevicePeriod * latency_type;
      std::size_t const buffer_size = (((SAMPLE_RATE * hnsBufferPeriod / 1'000'000) + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE;
      buffer0_.reset(new float[buffer_size]);
      buffer1_.reset(new float[buffer_size]);
      OBS_INFO("buffer size:  %ld [frames]", buffer_size);

      {
        WAVEFORMATEXTENSIBLE format;
        std::memset(&format, 0, sizeof(format));
        format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        format.Format.nChannels = 1;
        format.Format.nSamplesPerSec = SAMPLE_RATE;
        format.Format.wBitsPerSample = 32;
        format.Format.nBlockAlign = format.Format.wBitsPerSample / 8 * format.Format.nChannels;
        format.Format.nAvgBytesPerSec = format.Format.nSamplesPerSec * format.Format.nBlockAlign;
        format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        format.dwChannelMask = SPEAKER_FRONT_CENTER;
        format.Samples.wValidBitsPerSample = format.Format.wBitsPerSample;
        format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

        if FAILED(hr = pAudioClientIn_->Initialize(
          AUDCLNT_SHAREMODE_SHARED,
          AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
          hnsBufferPeriod,
          0,
          reinterpret_cast<WAVEFORMATEX*>(&format),
          nullptr))
        {
          std::string const& msg = std::system_category().message(hr);
          OBS_ERROR("unable to initialize audio pAudioClientIn: %s (%x)", msg.c_str(), hr);
          return hr;
        }
      }

      if FAILED(hr = pAudioClientIn_->SetEventHandle(hEvtAudioCaptureSamplesReady_))
      {
        std::string const& msg = std::system_category().message(hr);
        OBS_ERROR("unable to set ready event: %s (%x)", msg.c_str(), hr);
        return hr;
      }

      if FAILED(hr = pAudioClientIn_->GetService(IID_PPV_ARGS(&pCaptureClient_)))
      {
        std::string const& msg = std::system_category().message(hr);
        OBS_ERROR("unable to get new pCaptureClient pAudioClientIn: %s (%x)", msg.c_str(), hr);
        return hr;
      }

      if (hAudioThread_) {
        ::CloseHandle(hAudioThread_);
        hAudioThread_ = nullptr;
      }
      hAudioThread_ = ::CreateThread(nullptr, 0, OBSAudioSource::capture, this, 0, nullptr);
      if (!hAudioThread_) {
        hr = ::GetLastError();
        std::string const& msg = std::system_category().message(hr);
        OBS_ERROR("unable to create transport thread: %s (%x)", msg.c_str(), hr);
        return hr;
      }

      if FAILED(hr = pAudioClientIn_->Start()) {
        std::string const& msg = std::system_category().message(hr);
        OBS_ERROR("unable to start pCaptureClient pAudioClientIn: %s (%x)", msg.c_str(), hr);
        return hr;
      }

      return hr;
    }

    HRESULT Stop() {
      HRESULT hr = S_OK;

      ::SetEvent(hEvtShutdown_);
      ::WaitForSingleObject(hAudioThread_, INFINITE);
      if (pAudioClientIn_) {
          if FAILED(hr = pAudioClientIn_->Stop()) {
              std::string const& msg = std::system_category().message(hr);
              OBS_ERROR("Unable to stop audio pAudioClientIn: %s (%x)", msg.c_str(), hr);
              return hr;
          }
      }

      if (hEvtAudioCaptureSamplesReady_) {
        ::CloseHandle(hEvtAudioCaptureSamplesReady_);
        hEvtAudioCaptureSamplesReady_ = nullptr;
      }

      if (hEvtShutdown_) {
        ::CloseHandle(hEvtShutdown_);
        hEvtShutdown_ = nullptr;
      }

      if (hAudioThread_) {
        ::CloseHandle(hAudioThread_);
        hAudioThread_ = nullptr;
      }

      pCaptureClient_.Reset();
      pAudioClientIn_.Reset();
      pDevice_.Reset();

      return hr;
    }

    // パラメーターを更新する
    HRESULT Update(obs_data_t* settings) {
      HRESULT hr = S_OK;

      int const old_device_id = device_id_.load(std::memory_order::acquire);
      int const new_device_id = static_cast<int>(obs_data_get_int(settings, "device"));
      int const old_latency_mode = latency_mode_.load(std::memory_order::acquire);
      int const new_latency_mode = static_cast<int>(obs_data_get_int(settings, "latency"));
      if ((old_device_id != new_device_id) || (old_latency_mode != new_latency_mode)) {
        if (old_device_id >= 0) {
          if FAILED(hr = Stop()) {
            return hr;
          }
        }

        device_id_.store(new_device_id, std::memory_order::release);
        latency_mode_.store(new_latency_mode, std::memory_order::release);
        if FAILED(hr = Start()) {
          return hr;
        }
      }
      {
        // gain = 10 ** (db / 20)
        input_gain_.store(static_cast<float>(std::pow(10.0, obs_data_get_double(settings, "input_gain") * 0.05)), std::memory_order::release);
        output_gain_.store(static_cast<float>(std::pow(10.0, obs_data_get_double(settings, "output_gain") * 0.05)), std::memory_order::release);

        // cent = 1200 * log2(hz)
        pitch_shift_.store(static_cast<float>(obs_data_get_double(settings, "pitch_shift") * (std::log(2.0) / 1200.0)), std::memory_order::release);
        pitch_shift_mode_.store(static_cast<float>(obs_data_get_int(settings, "pitch_shift_mode")), std::memory_order::release);
        pitch_snap_.store(static_cast<float>(obs_data_get_double(settings, "pitch_snap") * 0.01), std::memory_order::release);
        primary_voice_.store(static_cast<int>(obs_data_get_int(settings, "primary_voice")), std::memory_order::release);
        secondary_voice_.store(static_cast<int>(obs_data_get_int(settings, "secondary_voice")), std::memory_order::release);
        amount_.store(static_cast<float>(obs_data_get_double(settings, "amount") * 0.01), std::memory_order::release);
      }
      return hr;
    }

    // 音声を処理する
    HRESULT Process() {
      HRESULT hr = S_OK;

      int const SAMPLE_RATE = sample_rate_;
      int const BLOCK_SIZE = block_size_;

      UINT uBufferSizeIn = 0;
      if FAILED(hr = pAudioClientIn_->GetBufferSize(&uBufferSizeIn)) {
        return hr;
      }
      OBS_INFO("uBufferSizeIn: %u", uBufferSizeIn);

      std::uint64_t total_frames = 0; ///< これまでの累積フレーム数
      std::uint32_t remainings = 0; ///< ブロックに満たない余っているサンプル数

      HANDLE events[] = { hEvtAudioCaptureSamplesReady_, hEvtShutdown_ };
      for (;;) {
        DWORD const result = ::WaitForMultipleObjects(static_cast<DWORD>(std::size(events)), events, FALSE, INFINITE);
        if (result == WAIT_OBJECT_0 + 1) {
          break;
        }

        float* pDataIn = nullptr;
        UINT32 uNumFrameToRead = 0; // shared mode での GetNextPacketSize と一致する。
        DWORD dwFlags = 0;
        if FAILED(hr = pCaptureClient_->GetBuffer(reinterpret_cast<BYTE**>(&pDataIn), &uNumFrameToRead, &dwFlags, nullptr, nullptr)) {
          return hr;
        }

        std::uint32_t const frames = remainings + uNumFrameToRead;
        std::uint32_t const block_count = frames / BLOCK_SIZE;
        std::uint32_t const block_frames = static_cast<uint32_t>(block_count * BLOCK_SIZE);
        std::uint64_t const timestamp = audio_frames_to_ns(SAMPLE_RATE, total_frames * BLOCK_SIZE);

        // ブロックに切り分ける (あまりは保存しておく)
        if (dwFlags & AUDCLNT_BUFFERFLAGS_SILENT) {
          if (block_count > 0) {
            // ブロックに書き込む
            std::size_t const new_frames = block_frames - remainings;
            std::memset(buffer0_.get() + remainings, 0, new_frames * sizeof(float));

            remainings = frames % BLOCK_SIZE;

            // あまりを覚えておく
            std::memset(buffer1_.get(), 0, remainings * sizeof(float));
            std::swap(buffer0_, buffer1_);
          }
          else {
            // あまりを追記する
            std::memset(buffer0_.get() + remainings, 0, uNumFrameToRead * sizeof(float));
            remainings = frames % BLOCK_SIZE;
          }
        }
        else {
          if (block_count > 0) {
            // ブロックに書き込む
            std::size_t const new_frames = block_frames - remainings;
            std::memcpy(buffer0_.get() + remainings, pDataIn, new_frames * sizeof(float));

            remainings = frames % BLOCK_SIZE;

            // あまりを覚えておく
            std::memcpy(buffer1_.get(), pDataIn + new_frames, remainings * sizeof(float));
            std::swap(buffer0_, buffer1_);
          }
          else {
            // あまりを追記する
            std::memcpy(buffer0_.get() + remainings, pDataIn, uNumFrameToRead * sizeof(float));
            remainings = frames % BLOCK_SIZE;
          }
        }
  
        if FAILED(hr = pCaptureClient_->ReleaseBuffer(uNumFrameToRead))
        {
          return hr;
        }
  
        // ブロック単位で処理
        if (block_count > 0) {
          int const id1 = primary_voice_.load(std::memory_order::acquire);
          int const id2 = secondary_voice_.load(std::memory_order::acquire);
          if (id2 < 0) {
            rtvc_set_voice(id1);
          }
          else
          {
            int const ids[] = { id1, id2 };
            float const amount = amount_.load(std::memory_order::acquire);
            float const amounts[] = { 1.0f - amount, amount };
            rtvc_set_voices(2, ids, amounts);
          }
          {
            float const params[] = {
              input_gain_.load(std::memory_order::acquire),
              output_gain_.load(std::memory_order::acquire),
              pitch_shift_.load(std::memory_order::acquire),
              pitch_shift_mode_.load(std::memory_order::acquire),
              pitch_snap_.load(std::memory_order::acquire),
            };
            constexpr int const num_params = static_cast<int>(std::size(params));
            float* out = buffer1_.get();
            for (std::uint32_t i = 0; i < block_count; ++i, out += BLOCK_SIZE, ++total_frames) {
              rtvc_process(num_params, params, out, out);
            }
          }
          {
            obs_source_audio data;
            std::memset(&data, 0, sizeof(data));
            data.data[0] = reinterpret_cast<std::uint8_t*>(buffer1_.get());
            data.frames = block_frames;
            data.speakers = SPEAKERS_MONO;
            data.format = AUDIO_FORMAT_FLOAT;
            data.samples_per_sec = SAMPLE_RATE;
            data.timestamp = os_gettime_ns(); // timestamp;
            obs_source_output_audio(context_, &data);
          }
        }
      }

      return hr;
    }

  public:
    static char const* get_name(void* type_data) {
      return "Real-Time Voice Changer";
    }

    // 構築する
    static void* create(obs_data_t* settings, obs_source_t* context)
    {
      std::unique_ptr<OBSAudioSource> _this(new OBSAudioSource(context));

      if FAILED(_this->Init()) {
        // TODO:
      }

      if FAILED(_this->Update(settings)) {
        // TODO:
      }

      return _this.release();
    }

    // 破棄する
    static void destroy(void* instance)
    {
      std::unique_ptr<OBSAudioSource> _this(reinterpret_cast<OBSAudioSource*>(instance));

      if FAILED(_this->Destroy()) {
        // TODO:
      }
    }

    // パラメーターのデフォルト値を設定する
    static void get_defaults(obs_data_t* settings)
    {
      obs_data_set_default_int(settings, "device", 0);
      obs_data_set_default_int(settings, "latency", static_cast<int>(1 + std::size(LATENCY_MODES) / 2));

      obs_data_set_default_double(settings, "input_gain", 0.0);
      obs_data_set_default_double(settings, "output_gain", 0.0);
      obs_data_set_default_double(settings, "pitch_shift", 0.0);
      obs_data_set_default_int(settings, "pitch_shift_mode", 1);
      obs_data_set_default_double(settings, "pitch_snap", 0.0);
      obs_data_set_default_int(settings, "primary_voice", 100);
      obs_data_set_default_int(settings, "secondary_voice", -1);
      obs_data_set_default_double(settings, "amount", 0.0);
    }

    // パラメーターを定義する
    static obs_properties_t* get_properties(void* instance)
    {
      OBSAudioSource* _this = reinterpret_cast<OBSAudioSource*>(instance);
      if (_this) {
        obs_properties_t* props = obs_properties_create();
        if FAILED(_this->GetProperties(*props)) {
          // TODO:
        }
        return props;
      }
      return nullptr;
    }

    // パラメーターを更新する
    static void update(void* instance, obs_data_t* settings)
    {
      OBSAudioSource* _this = reinterpret_cast<OBSAudioSource*>(instance);
      if (_this) {
        if FAILED(_this->Update(settings)) {
          // TODO:
        }
      }
    }

    // WASAPI Thread
    static DWORD WINAPI capture(void* instance) {
      HRESULT hr = S_OK;

      if FAILED(hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {
        std::string const& msg = std::system_category().message(hr);
        OBS_ERROR("Unable to initialize COM in render thread: %s (%x)", msg.c_str(), hr);
        return hr;
      }

      struct com_guard {
        ~com_guard() noexcept {
          ::CoUninitialize();
        }
      } _com_guard;

      {
        DWORD taskIndex = 0;
        HANDLE hMmCss = ::AvSetMmThreadCharacteristics(TEXT("Pro Audio"), &taskIndex);
        if (!hMmCss) {
          HANDLE hMmCss = ::AvSetMmThreadCharacteristics(TEXT("Audio"), &taskIndex);
          if (!hMmCss) {
            hr = GetLastError();
            std::string const& msg = std::system_category().message(hr);
            OBS_ERROR("AvSetMmThreadCharacteristics: %s (%x)", msg.c_str(), hr);
            return hr;
          }
        }

        struct mm_thread_guard {
          mm_thread_guard(HANDLE hMmCss) : hMmCss_(hMmCss) {}
          ~mm_thread_guard() noexcept {
            ::AvRevertMmThreadCharacteristics(hMmCss_);
          }
          HANDLE hMmCss_;
        } _mm_thread_guard(hMmCss);

        {
          OBSAudioSource* _this = reinterpret_cast<OBSAudioSource*>(instance);
          if (_this) {
            if FAILED(hr = _this->Process()) {
              return hr;
            }
          }
        }
      }
      return hr;
    }

  private:
    int sample_rate_ = 24'000;
    int block_size_ = 256;

    obs_source_t* context_;

    Microsoft::WRL::ComPtr<IMMDeviceCollection> pDeviceCollection_;
    Microsoft::WRL::ComPtr<IMMDevice> pDevice_;

    int device_count_ = 0;
    std::atomic<int> device_id_ = -1;
    std::atomic<int> latency_mode_ = static_cast<int>(1 + std::size(LATENCY_MODES) / 2);

    std::atomic<int> primary_voice_ = 0;
    std::atomic<float> amount_ = 0.0f;
    std::atomic<int> secondary_voice_ = 0;

    std::atomic<float> input_gain_ = 1.0f;
    std::atomic<float> output_gain_ = 1.0f;
    std::atomic<float> pitch_shift_ = 0.0f;
    std::atomic<float> pitch_shift_mode_ = 1.0f;
    std::atomic<float> pitch_snap_ = 0.0f;

    HANDLE hEvtAudioCaptureSamplesReady_ = nullptr;
    HANDLE hEvtShutdown_ = nullptr;
    HANDLE hAudioThread_ = nullptr;
    Microsoft::WRL::ComPtr<IAudioClient3> pAudioClientIn_;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> pCaptureClient_;

    std::unique_ptr<float[]> buffer0_;
    std::unique_ptr<float[]> buffer1_;
  };
}

extern "C" {
  OBS_DECLARE_MODULE()
  OBS_MODULE_USE_DEFAULT_LOCALE("nair-rtvc-source", "en-US")

  bool obs_module_load(void)
  {
    OBS_INFO("plugin loaded successfully (version 1.0.4)");

    obs_source_info info;
    std::memset(&info, 0, sizeof(info));
    info.id             = "nair-rtvc-source";
    info.type           = OBS_SOURCE_TYPE_INPUT;
    info.output_flags   = OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE;
    info.get_name       = OBSAudioSource::get_name;
    info.create         = OBSAudioSource::create;
    info.destroy        = OBSAudioSource::destroy;
    info.get_defaults   = OBSAudioSource::get_defaults;
    info.get_properties = OBSAudioSource::get_properties;
    info.update         = OBSAudioSource::update;
    // info.icon_type      = OBS_ICON_TYPE_AUDIO_INPUT;
    obs_register_source(&info);

    return true;
  }
}

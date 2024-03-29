#include <stdio.h>
#include <stdlib.h>

#define INITGUID
#define COBJMACROS
#define CONST_VTABLE

#include <windows.h>
//#include <winioctl.h>
#include <ntdef.h>
#include <winbase.h>
#include <setupapi.h>

#include <mmdeviceapi.h>
#include <audioclient.h>

#include <hidsdi.h>

#include <xaudio2.h>

#include <devpkey.h>

DEFINE_PROPERTYKEY(PKEY_Device_FriendlyName, 0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 14);
DEFINE_PROPERTYKEY(PKEY_Device_ContainerId, 0x8c7ed206,0x3f8a,0x4827,0xb3,0xab,0xae,0x9e,0x1f,0xae,0xfc,0x6c, 2);

GUID HIDInterfaceClassGuid           = { 0x4d1e55b2, 0xf16f, 0x11cf, {0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30} };
GUID AudioRendererInterfaceClassGuid = { 0xe6327cad, 0xdcec, 0x4949, {0xae, 0x8a, 0x99, 0x1e, 0x97, 0x6a, 0x79, 0xd2} };

struct DualsenseInfo {
  WCHAR *serialNumber;
  WCHAR *manufacturer;
  WCHAR *product;
  WCHAR *instanceID;
  GUID *containerID;
  WCHAR *class;
  WCHAR *driverKey;
  WCHAR *friendlyName;
  WCHAR *deviceDesc;
  GUID *classGUID;
  WCHAR *hardwareID;
};

void fill_dualsense_hidd_attributes(struct DualsenseInfo *dualsense, HANDLE handle) {
    WCHAR wstr[256];

    if (HidD_GetSerialNumberString(handle, wstr, sizeof(wstr))) {
        wstr[255] = 0;
        dualsense->serialNumber = wcsdup(wstr);
    }
    if (HidD_GetManufacturerString(handle, wstr, sizeof(wstr))) {
        wstr[255] = 0;
        dualsense->manufacturer = wcsdup(wstr);
    }
    if (HidD_GetProductString(handle, wstr, sizeof(wstr))) {
        wstr[255] = 0;
        dualsense->product = wcsdup(wstr);
    }
}

void fill_dualsense_hid_setupdi_props(struct DualsenseInfo *dualsense, HDEVINFO device_info_set) {
    DWORD required_size;
    WCHAR guidstr[39] = { 0, 0, };
    WCHAR buffer[4096];
    SP_DEVINFO_DATA devinfo_data;

    memset(&devinfo_data, 0x0, sizeof(devinfo_data));
    devinfo_data.cbSize = sizeof(SP_DEVINFO_DATA);

    for (int j=0; ; j++) {
        if (!SetupDiEnumDeviceInfo(device_info_set, j, &devinfo_data))
            break;

        /* I actually don't think that's how games find it, XInput and other stuff may be involved, but it should be a good enough shortcut */
        if (!SetupDiGetDeviceInstanceIdW(device_info_set, &devinfo_data, buffer, 4096, &required_size))
            continue;

        if (!wcsstr(buffer, L"HID\\VID_054C&PID_0CE6"))
            continue;

        if (dualsense->instanceID) {
          free(dualsense->instanceID);
        }
        dualsense->instanceID = wcsdup(buffer);

        DWORD reg_data_type = 0;
        /* query SPDRP_BASE_CONTAINERID */
        if (!SetupDiGetDeviceRegistryPropertyW(device_info_set, &devinfo_data, 36, &reg_data_type, (PBYTE)guidstr, sizeof(guidstr), &required_size)) {
            guidstr[0] = 0;
        } else {
          if (guidstr[0]) {
            dualsense->containerID = malloc(sizeof(GUID));
            CLSIDFromString(guidstr, dualsense->containerID);
          }
        }

        if (SetupDiGetDeviceRegistryPropertyW(device_info_set, &devinfo_data, SPDRP_CLASS, &reg_data_type, (PBYTE)buffer, 4096, &required_size)) {
          dualsense->class = wcsdup(buffer);
        }

        if (SetupDiGetDeviceRegistryPropertyW(device_info_set, &devinfo_data, SPDRP_DRIVER, &reg_data_type, (PBYTE)buffer, 4096, &required_size)) {
          dualsense->driverKey = wcsdup(buffer);
        }

        if (SetupDiGetDeviceRegistryPropertyW(device_info_set, &devinfo_data, SPDRP_FRIENDLYNAME, &reg_data_type, (PBYTE)buffer, 4096, &required_size)) {
          dualsense->friendlyName = wcsdup(buffer);
        }

        if (SetupDiGetDeviceRegistryPropertyW(device_info_set, &devinfo_data, SPDRP_DEVICEDESC, &reg_data_type, (PBYTE)buffer, 4096, &required_size)) {
          dualsense->deviceDesc = wcsdup(buffer);
        }

        if (SetupDiGetDeviceRegistryPropertyW(device_info_set, &devinfo_data, SPDRP_HARDWAREID, &reg_data_type, (PBYTE)buffer, 4096, &required_size)) {
          dualsense->hardwareID = malloc(required_size);
          memcpy(dualsense->hardwareID, buffer, required_size);
        }

        if (!SetupDiGetDeviceRegistryPropertyW(device_info_set, &devinfo_data, SPDRP_CLASSGUID, &reg_data_type, (PBYTE)guidstr, sizeof(guidstr), &required_size)) {
            guidstr[0] = 0;
        } else {
          if (guidstr[0]) {
            dualsense->classGUID = malloc(sizeof(GUID));
            CLSIDFromString(guidstr, dualsense->classGUID);
          }
        }
    }
}

struct DualsenseInfo *find_hid_device(WORD vendor_id, WORD product_id) {
    HDEVINFO device_info_set = INVALID_HANDLE_VALUE;
    device_info_set = SetupDiGetClassDevsW(&HIDInterfaceClassGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    for (DWORD member_index = 0; ; member_index++) {
        SP_DEVICE_INTERFACE_DATA device_interface_data;

        memset(&device_interface_data, 0, sizeof(SP_DEVICE_INTERFACE_DATA));
        device_interface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        if (SetupDiEnumDeviceInterfaces(device_info_set, NULL, &HIDInterfaceClassGuid, member_index, &device_interface_data)) {
            SP_DEVICE_INTERFACE_DETAIL_DATA_W *device_interface_detail_data = NULL;
            DWORD required_size;

            /* Get required size */
            SetupDiGetDeviceInterfaceDetailW(device_info_set, &device_interface_data, NULL, 0, &required_size, NULL);

            device_interface_detail_data = calloc(1, required_size);
            device_interface_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

            /* Get actual interface detail data */
            if (!SetupDiGetDeviceInterfaceDetailW(device_info_set, &device_interface_data, device_interface_detail_data, required_size, NULL, NULL)) {
                free(device_interface_detail_data);
                continue;
            }

            if (!device_interface_detail_data->DevicePath) {
                free(device_interface_detail_data);
                continue;
            }

            HANDLE handle = CreateFileW(device_interface_detail_data->DevicePath, GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0);
            if (handle == INVALID_HANDLE_VALUE) {
               free(device_interface_detail_data);
               continue;
            }

            HIDD_ATTRIBUTES attrib;
            attrib.Size = sizeof(HIDD_ATTRIBUTES);

            if (HidD_GetAttributes(handle, &attrib) && attrib.VendorID == vendor_id && attrib.ProductID == product_id) {
                // Found the controller, fill info

                struct DualsenseInfo *dualsenseHidInfo = calloc(1, sizeof(struct DualsenseInfo));
                fill_dualsense_hidd_attributes(dualsenseHidInfo, handle);
                fill_dualsense_hid_setupdi_props(dualsenseHidInfo, device_info_set);

                CloseHandle(handle);

                return dualsenseHidInfo;
            }

            CloseHandle(handle);
        } else {
            if (ERROR_NO_MORE_ITEMS == GetLastError());
                break;
        }
    }

    return NULL;
}


BOOL find_audio_render_by(const WCHAR *friendly_name, const GUID *container_id, int *nb_matches, LPWSTR *devid, GUID **out_container_id, WAVEFORMATEX **device_format, WAVEFORMATEX **fmt, REFERENCE_TIME *defperiod, REFERENCE_TIME *minperiod) {
    IMMDeviceEnumerator *it;
    IMMDeviceCollection *devs;
    HRESULT hr;
    UINT count;

    *nb_matches = 0;

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                          &IID_IMMDeviceEnumerator, (void*) &it);

    if (FAILED(hr))
        return FALSE;

    hr = IMMDeviceEnumerator_EnumAudioEndpoints(it, eRender, 15, &devs);
    if (FAILED(hr))
        return FALSE;

    hr = IMMDeviceCollection_GetCount(devs, &count);
    if (FAILED(hr))
        return FALSE;

    for (UINT i = 0; i < count; i++) {
        IPropertyStore *props;
        IMMDevice *dev;
        IAudioClient *audio_client = NULL;

        hr = IMMDeviceCollection_Item(devs, i, &dev);
        if (FAILED(hr))
            continue;

        hr = IMMDevice_OpenPropertyStore(dev, STGM_READ, &props);
        if (FAILED(hr)) {
            IMMDevice_Release(dev);
            continue;
        }

        PROPVARIANT v;

        if (friendly_name) {
            PropVariantInit(&v);
            hr = IPropertyStore_GetValue(props, &PKEY_Device_FriendlyName, &v);
            if (FAILED(hr) || v.vt != VT_LPWSTR && !v.pwszVal || !wcsstr(v.pwszVal, friendly_name)) {
                IMMDevice_Release(dev);
                continue;
            }
        }

        if (container_id || out_container_id) {
            PropVariantInit(&v);
            hr = IPropertyStore_GetValue(props, &PKEY_Device_ContainerId, &v);
            if (SUCCEEDED(hr) && v.vt == VT_CLSID) {
                *out_container_id = malloc(sizeof(GUID));
                memcpy(*out_container_id, v.puuid, sizeof(GUID));
            }
            if (container_id && (FAILED(hr) || v.vt != VT_CLSID || !IsEqualGUID(v.puuid, container_id)))
            {
               IMMDevice_Release(dev);
               continue;
            }
        }

        if (device_format) {
            PropVariantInit(&v);
            hr = IPropertyStore_GetValue(props, &PKEY_AudioEngine_DeviceFormat, &v);
            if (SUCCEEDED(hr) && v.vt == VT_BLOB) {
                *device_format = malloc(v.blob.cbSize);
                memcpy(*device_format, v.blob.pBlobData, v.blob.cbSize);
            }
        }

        *nb_matches += 1;

        hr = IMMDevice_GetId(dev, devid);
        if (FAILED(hr))
        {
            IMMDevice_Release(dev);
            continue;
        }

        hr = IMMDevice_Activate(dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (LPVOID*)&audio_client);
        if (FAILED(hr)) {
            IMMDevice_Release(dev);
            CoTaskMemFree(*devid);
            *devid = NULL;
            continue;
        }

        if (FAILED(IAudioClient_GetMixFormat(audio_client, fmt))) {
            IAudioClient_Release(audio_client);
            IMMDevice_Release(dev);
            CoTaskMemFree(*devid);
            *devid = NULL;
            continue;
        }

        if (FAILED(IAudioClient_GetDevicePeriod(audio_client, defperiod, minperiod))) {
            IAudioClient_Release(audio_client);
            IMMDevice_Release(dev);
            CoTaskMemFree(*devid);
            *devid = NULL;
            continue;
        }

        IAudioClient_Release(audio_client);
        IMMDevice_Release(dev);
        IMMDeviceCollection_Release(devs);

        return TRUE;
    }
    IMMDeviceCollection_Release(devs);

    return FALSE;
}

#if 0
BOOL spiderman_find_unknown(LPWSTR expected_id) {
    // TODO: I'm actually not sure what Spider-Man *really* does here

    WCHAR expected_instance_id[] = L"SWD\\MMDEVAPI\\{0.0.0.00000000}.{12345678-1234-1234-1234-123456789123}";
    HDEVINFO device_info_set = INVALID_HANDLE_VALUE;
    SP_DEVICE_INTERFACE_DETAIL_DATA_W *device_interface_detail_data = NULL;
    device_info_set = SetupDiGetClassDevsExW(&HIDInterfaceClassGuid, NULL, 0, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE, NULL, NULL, 0);

    for (DWORD member_index = 0; ; member_index++) {
        SP_DEVICE_INTERFACE_DATA device_interface_data;

        memset(&device_interface_data, 0, sizeof(SP_DEVICE_INTERFACE_DATA));
        device_interface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        if (SetupDiEnumDeviceInterfaces(device_info_set, NULL, &HIDInterfaceClassGuid, member_index, &device_interface_data)) {
            DEVPROPTYPE prop_type;
            SP_DEVINFO_DATA devinfo_data;
            DWORD required_size;

            memset(&devinfo_data, 0x0, sizeof(devinfo_data));
            devinfo_data.cbSize = sizeof(SP_DEVINFO_DATA);

            /* Get required size */
            SetupDiGetDeviceInterfaceDetailW(device_info_set, &device_interface_data, NULL, 0, &required_size, NULL);

            device_interface_detail_data = calloc(1, required_size);
            device_interface_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

            /* Get actual interface detail data */
            if (!SetupDiGetDeviceInterfaceDetailW(device_info_set, &device_interface_data, device_interface_detail_data, required_size, NULL, &devinfo_data)) {
                free(device_interface_detail_data);
                device_interface_detail_data = NULL;
                continue;
            }

            wprintf(L"DEBUG interface data: %S\n", &device_interface_detail_data->DevicePath);

            WCHAR buffer[4096];
            if (!SetupDiGetDeviceInterfacePropertyW(device_info_set, &device_interface_data, DEVPKEY_Device_InstanceId, &prop_type, buffer, 510, NULL, 0)) {
                free(device_interface_detail_data);
                device_interface_detail_data = NULL;
                continue;
            }

            wprintf(L"DEBUG: %S\n", buffer);

            if (wcscmp(buffer, expected_instance_id) == 0) {
                break;
            }

            free(device_interface_detail_data);
            device_interface_detail_data = NULL;
        } else {
            if (ERROR_NO_MORE_ITEMS == GetLastError());
                break;
        }
    }
}
#endif

BOOL deathloop_find_speaker(LPWSTR expected_id) {
    HMODULE mXAudio2;
    WCHAR expected_instance_id[] = L"SWD\\MMDEVAPI\\{0.0.0.00000000}.{12345678-1234-1234-1234-123456789123}";
    HDEVINFO device_info_set = INVALID_HANDLE_VALUE;
    SP_DEVICE_INTERFACE_DETAIL_DATA_W *device_interface_detail_data = NULL;
    device_info_set = SetupDiGetClassDevsExW(&AudioRendererInterfaceClassGuid, NULL, 0, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE, NULL, NULL, 0);

    if (!(mXAudio2 = LoadLibraryExW(L"XAudio2_9.dll", NULL, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS))
            && !(mXAudio2 = LoadLibraryExW(L"XAudio2_8.dll", NULL, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS))) {
        wprintf(L"INFO: could not load XAudio2 (0x%x), Deathloop will work by falling back to other APIs, but other games might have issues\n", GetLastError());
        return FALSE;
    }

    if (!expected_id)
        return FALSE;

    if (wcslen(expected_id) != 55) {
        wprintf(L"WARNING: invalid MMDevice ID: %S\n", expected_id);
        return FALSE;
    }

    wcscpy(expected_instance_id+13, expected_id);
    wcsupr(expected_instance_id);

    for (DWORD member_index = 0; ; member_index++) {
        SP_DEVICE_INTERFACE_DATA device_interface_data;

        memset(&device_interface_data, 0, sizeof(SP_DEVICE_INTERFACE_DATA));
        device_interface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        if (SetupDiEnumDeviceInterfaces(device_info_set, NULL, &AudioRendererInterfaceClassGuid, member_index, &device_interface_data)) {
            SP_DEVINFO_DATA devinfo_data;
            DWORD required_size;

            memset(&devinfo_data, 0x0, sizeof(devinfo_data));
            devinfo_data.cbSize = sizeof(SP_DEVINFO_DATA);

            /* Get required size */
            SetupDiGetDeviceInterfaceDetailW(device_info_set, &device_interface_data, NULL, 0, &required_size, NULL);

            device_interface_detail_data = calloc(1, required_size);
            device_interface_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

            /* Get actual interface detail data */
            if (!SetupDiGetDeviceInterfaceDetailW(device_info_set, &device_interface_data, device_interface_detail_data, required_size, NULL, &devinfo_data)) {
                free(device_interface_detail_data);
                device_interface_detail_data = NULL;
                continue;
            }

            WCHAR buffer[4096];
            if (!SetupDiGetDeviceInstanceIdW(device_info_set, &devinfo_data, buffer, 4096, &required_size)) {
                free(device_interface_detail_data);
                device_interface_detail_data = NULL;
                continue;
            }

            if (wcscmp(buffer, expected_instance_id) == 0) {
                break;
            }

            free(device_interface_detail_data);
            device_interface_detail_data = NULL;
        } else {
            if (ERROR_NO_MORE_ITEMS == GetLastError());
                break;
        }
    }

    if (device_interface_detail_data && device_interface_detail_data->DevicePath) {
        /* TODO: not sure how to use XAudio2 in C */
        // IXAudio2 *xaudio2 = NULL;
        // XAudio2Create(&xaudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
        // xaudio2->CreateMasteringVoice(…, XAUDIO2_DEFAULT_CHANNELS, 48000, 0, device_interface_detail_data->DevicePath, 0, AudioCategory_GameEffects)
        wprintf(L"WARNING: Speaker will work in Deathloop only if it manages to open %S in XAudio2::CreateMasteringVoice (not implemented at the moment I'm writing this)\n", device_interface_detail_data->DevicePath);
    } else {
        wprintf(L"WARNING: Audio device not found in SetupApi, Deathloop will fall back to default output.\n");
    }

    FreeLibrary(mXAudio2);
}

void dump_fmt(WAVEFORMATEX *fmt) {
    wprintf(L"  format:\n");
    wprintf(L"    wFormatTag: 0x%x (", fmt->wFormatTag);
        switch(fmt->wFormatTag) {
        case WAVE_FORMAT_PCM:
            wprintf(L"WAVE_FORMAT_PCM");
            break;
        case WAVE_FORMAT_IEEE_FLOAT:
            wprintf(L"WAVE_FORMAT_IEEE_FLOAT");
            break;
        case WAVE_FORMAT_EXTENSIBLE:
            wprintf(L"WAVE_FORMAT_EXTENSIBLE");
            break;
        default:
            wprintf(L"Unknown");
            break;
        }
        wprintf(L")\n");

        wprintf(L"    nChannels: %u\n", fmt->nChannels);
        wprintf(L"    nSamplesPerSec: %lu\n", fmt->nSamplesPerSec);
        wprintf(L"    nAvgBytesPerSec: %lu\n", fmt->nAvgBytesPerSec);
        wprintf(L"    nBlockAlign: %u\n", fmt->nBlockAlign);
        wprintf(L"    wBitsPerSample: %u\n", fmt->wBitsPerSample);
        wprintf(L"    cbSize: %u\n", fmt->cbSize);

        if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            WAVEFORMATEXTENSIBLE *fmtex = (void*)fmt;
            wprintf(L"    dwChannelMask: %08lx\n", fmtex->dwChannelMask);
            wprintf(L"    Samples: %04x\n", fmtex->Samples.wReserved);
            LPOLESTR lplpsz;
            StringFromCLSID(&fmtex->SubFormat, &lplpsz);
            wprintf(L"    SubFormat: %S\n", lplpsz);
        }
}


int main(void)
{
    struct DualsenseInfo *dualsense = NULL;
    int nb_matches;

    /* First, find the controller HID */
    wprintf(L"Searching for the HID controller...\n");
    if (!(dualsense = find_hid_device(0x054c, 0x0ce6))) {
        wprintf(L"HID controller not found! Aborting.\n");
        return 1;
    }

    wprintf(L"DualSense controller found:\n");
    if (dualsense->manufacturer)
        wprintf(L"  Manufacturer: %S\n", dualsense->manufacturer);
    if (dualsense->product)
        wprintf(L"  Product name: %S\n", dualsense->product);
    if (dualsense->serialNumber)
        wprintf(L"  Serial number: %S\n", dualsense->serialNumber);
    if (dualsense->instanceID) {
        wprintf(L"  InstanceID: %S\n", dualsense->instanceID);
    }
    if (dualsense->containerID) {
        WCHAR wstr[39];
        StringFromGUID2(dualsense->containerID, wstr, 39*2);
        wprintf(L"  ContainerID: %S\n", wstr);
    }
    if (dualsense->class) {
        wprintf(L"  SPDRP_CLASS: %S\n", dualsense->class);
    }
    if (dualsense->hardwareID) {
      wprintf(L"  SDRP_HARDWAREID:\n");
      for (int offset=0; dualsense->hardwareID[offset]; offset += wcslen(&dualsense->hardwareID[offset])) {
        wprintf(L"    %S\n", &dualsense->hardwareID[offset]);
      }
    }
    if (dualsense->driverKey) {
        wprintf(L"  SPDRP_DRIVER: %S\n", dualsense->driverKey);
    }
    if (dualsense->friendlyName) {
        wprintf(L"  SPDRP_FRIENDLYNAME: %S\n", dualsense->friendlyName);
    }
    if (dualsense->deviceDesc) {
        wprintf(L"  SPDRP_DEVICEDESC: %S\n", dualsense->deviceDesc);
    }
    if (dualsense->classGUID) {
        WCHAR wstr[39];
        StringFromGUID2(dualsense->classGUID, wstr, 39*2);
        wprintf(L"  SPDRP_CLASSGUID: %S\n", wstr);
    }
    if (!dualsense->containerID)
        wprintf(L"WARNING: ContainerID not set, although this is required for most games\n");

    GUID *audio_containerID = NULL;

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    WAVEFORMATEX *fmt, *device_fmt;
    REFERENCE_TIME defperiod, minperiod;
    LPWSTR devid = NULL;

    wprintf(L"\n\nSearching for audio output based on FriendlyName (Final Fantasy XIV Online, Final Fantasy VII Remake Intergrade)...\n");
    if (find_audio_render_by(L"Wireless Controller", NULL, &nb_matches, &devid, &audio_containerID, &device_fmt, &fmt, &defperiod, &minperiod)) {
        wprintf(L"Found audio output!\n");
        wprintf(L"  Device ID: %S\n", devid);
        if (audio_containerID) {
            WCHAR wstr[39];
            StringFromGUID2(audio_containerID, wstr, 39*2);
            wprintf(L"  ContainerID: %S\n", wstr);
        }
        dump_fmt(device_fmt);
        dump_fmt(fmt);
        if (fmt->nChannels != 4)
            wprintf(L"WARNING: audio device does not have 4 channels, this is going to cause issues\n");
    } else {
        wprintf(L"WARNING: audio output not found, haptics and speaker out won't work for Final Fantasy XIV Online and Final Fantasy VII Remake Intergrade\n");
    }

    if(dualsense->containerID) {
        wprintf(L"\n\nSearching for audio output based on ContainerID (audio-based haptics in Ghostwire: Tokyo, Deathloop, ...)\n");
        if (find_audio_render_by(NULL, dualsense->containerID, &nb_matches, &devid, &audio_containerID, &device_fmt, &fmt, &defperiod, &minperiod)) {
            wprintf(L"Found audio output!\n");
            wprintf(L"  Device ID: %S\n", devid);
            if (audio_containerID) {
                WCHAR wstr[39];
                StringFromGUID2(audio_containerID, wstr, 39*2);
                wprintf(L"  ContainerID: %S\n", wstr);
            }
            dump_fmt(device_fmt);
            dump_fmt(fmt);
            if (fmt->nChannels != 4)
                wprintf(L"WARNING: audio device does not have 4 channels, this is going to cause issues\n");
            if (nb_matches != 1)
                wprintf(L"WARNING: the audio device was not the first match, this will make games like Deathloop fail to use it\n");
        } else {
            wprintf(L"WARNING: audio output not found, haptics won't work for Ghostwire: Tokyo, Deathloop and other Wwise-based games\n");
        }
    } else {
        wprintf(L"\n\nWARNING: Cannot search for audio output based on ContainerID (audio-based haptics will not work in Ghostwire: Tokyo, Deathloop, ...)\n");
    }

    if (devid) {
        wprintf(L"\n\nOpening audio output through SetupDi+XAudio2 (Deathloop):\n");
        deathloop_find_speaker(devid);
    } else {
        wprintf(L"\n\nWARNING: Audio device unknown, cannot be searched for speaker access\n");
    }

    return 0;
}

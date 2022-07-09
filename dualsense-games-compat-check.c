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

DEFINE_PROPERTYKEY(PKEY_Device_FriendlyName, 0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 14);
DEFINE_PROPERTYKEY(PKEY_Device_ContainerId, 0x8c7ed206,0x3f8a,0x4827,0xb3,0xab,0xae,0x9e,0x1f,0xae,0xfc,0x6c, 2);

GUID HIDInterfaceClassGuid           = { 0x4d1e55b2, 0xf16f, 0x11cf, {0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30} };
GUID AudioRendererInterfaceClassGuid = { 0xe6327cad, 0xdcec, 0x4949, {0xae, 0x8a, 0x99, 0x1e, 0x97, 0x6a, 0x79, 0xd2} };

BOOL find_hid_device(WORD vendor_id, WORD product_id, WCHAR **serial_number, WCHAR **manufacturer, WCHAR **product, GUID **containerID) {
    HDEVINFO device_info_set = INVALID_HANDLE_VALUE;
    device_info_set = SetupDiGetClassDevsW(&HIDInterfaceClassGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    for (DWORD member_index = 0; ; member_index++) {
        SP_DEVICE_INTERFACE_DATA device_interface_data;

        memset(&device_interface_data, 0, sizeof(SP_DEVICE_INTERFACE_DATA));
        device_interface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        if (SetupDiEnumDeviceInterfaces(device_info_set, NULL, &HIDInterfaceClassGuid, member_index, &device_interface_data)) {
            WCHAR guidstr[39] = { 0, 0, };
            SP_DEVINFO_DATA devinfo_data;
            SP_DEVICE_INTERFACE_DETAIL_DATA_W *device_interface_detail_data = NULL;
            DWORD required_size;

            memset(&devinfo_data, 0x0, sizeof(devinfo_data));
            devinfo_data.cbSize = sizeof(SP_DEVINFO_DATA);

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

            for (int j=0; ; j++) {
                WCHAR buffer[4096];
                if (!SetupDiEnumDeviceInfo(device_info_set, j, &devinfo_data))
                    break;

                /* I actually don't think that's how games find it, XInput and other stuff may be involved, but it should be a good enough shortcut */
                if (!SetupDiGetDeviceInstanceIdW(device_info_set, &devinfo_data, buffer, 4096, &required_size))
                    continue;

                if (!wcsstr(buffer, L"HID\\VID_054C&PID_0CE6"))
                    continue;

                DWORD reg_data_type = 0;
                /* query SPDRP_BASE_CONTAINERID */
                if (!SetupDiGetDeviceRegistryPropertyW(device_info_set, &devinfo_data, 36, &reg_data_type, (PBYTE)guidstr, sizeof(guidstr), &required_size)) {
                    guidstr[0] = 0;
                }
            }

            HANDLE handle = CreateFileW(device_interface_detail_data->DevicePath, GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0);
            if (handle == INVALID_HANDLE_VALUE) {
               free(device_interface_detail_data);
               continue;
            }

            HIDD_ATTRIBUTES attrib;
            attrib.Size = sizeof(HIDD_ATTRIBUTES);

            if (HidD_GetAttributes(handle, &attrib) && attrib.VendorID == vendor_id && attrib.ProductID == product_id) {
                WCHAR wstr[256];

                if (serial_number && HidD_GetSerialNumberString(handle, wstr, sizeof(wstr))) {
                    wstr[255] = 0;
                    *serial_number = wcsdup(wstr);
                }
                if (manufacturer && HidD_GetManufacturerString(handle, wstr, sizeof(wstr))) {
                    wstr[255] = 0;
                    *manufacturer = wcsdup(wstr);
                }
                if (product && HidD_GetProductString(handle, wstr, sizeof(wstr))) {
                    wstr[255] = 0;
                    *product = wcsdup(wstr);
                }

                if (guidstr[0]) {
                    *containerID = malloc(sizeof(GUID));
                    CLSIDFromString(guidstr, *containerID);
                }

                CloseHandle(handle);

                return TRUE;
            }

            CloseHandle(handle);
        } else {
            if (ERROR_NO_MORE_ITEMS == GetLastError());
                break;
        }
    }

    return FALSE;
}


BOOL find_audio_render_by(const WCHAR *friendly_name, const GUID *container_id, LPWSTR *devid, GUID **out_container_id, WAVEFORMATEX **fmt, REFERENCE_TIME *defperiod, REFERENCE_TIME *minperiod) {
    IMMDeviceEnumerator *it;
    IMMDeviceCollection *devs;
    HRESULT hr;
    UINT count;

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

BOOL deathloop_find_speaker_wip() {
    HDEVINFO device_info_set = INVALID_HANDLE_VALUE;
    device_info_set = SetupDiGetClassDevsExW(&AudioRendererInterfaceClassGuid, NULL, 0, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE, NULL, NULL, 0);

    for (DWORD member_index = 0; ; member_index++) {
        SP_DEVICE_INTERFACE_DATA device_interface_data;

        memset(&device_interface_data, 0, sizeof(SP_DEVICE_INTERFACE_DATA));
        device_interface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        if (SetupDiEnumDeviceInterfaces(device_info_set, NULL, &AudioRendererInterfaceClassGuid, member_index, &device_interface_data)) {
            SP_DEVINFO_DATA devinfo_data;
            SP_DEVICE_INTERFACE_DETAIL_DATA_W *device_interface_detail_data = NULL;
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
                continue;
            }

            WCHAR buffer[4096];
            if (!SetupDiGetDeviceInstanceIdW(device_info_set, &devinfo_data, buffer, 4096, &required_size)) {
                free(device_interface_detail_data);
                continue;
            }

            WCHAR *guidstr;
            StringFromCLSID(&device_interface_data.InterfaceClassGuid, &guidstr);

            wprintf(L"Device %d:\n", member_index);
            wprintf(L"  InterfaceClassGuid: %S\n", guidstr);
            wprintf(L"  Path: %S\n", device_interface_detail_data->DevicePath);
            wprintf(L"  InstanceID: %S\n", buffer);
        } else {
            if (ERROR_NO_MORE_ITEMS == GetLastError());
                break;
        }
    }
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
    WCHAR *serial_number = NULL, *manufacturer = NULL, *product = NULL;
    GUID *containerID = NULL;
    GUID *audio_containerID = NULL;

    /* First, find the controller HID */
    wprintf(L"Searching for the HID controller...\n");
    if (!find_hid_device(0x054c, 0x0ce6, &serial_number, &manufacturer, &product, &containerID)) {
        wprintf(L"HID controller not found! Aborting.\n");
        return 1;
    }

    wprintf(L"DualSense controller found:\n");
    if (manufacturer)
        wprintf(L"  Manufacturer: %S\n", manufacturer);
    if (product)
        wprintf(L"  Product name: %S\n", product);
    if (serial_number)
        wprintf(L"  Serial number: %S\n", serial_number);
    if (containerID) {
        WCHAR wstr[39];
        StringFromGUID2(containerID, wstr, 39*2);
        wprintf(L"  ContainerID: %S\n", wstr);
    }
    if (!containerID)
        wprintf(L"WARNING: ContainerID not set, although this is required for most games\n");

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    WAVEFORMATEX *fmt;
    REFERENCE_TIME defperiod, minperiod;
    LPWSTR devid = NULL;

    wprintf(L"\n\nSearching for audio output based on FriendlyName (Final Fantasy XIV Online, Final Fantasy VII Remake Intergrade)...\n");
    if (find_audio_render_by(L"Wireless Controller", NULL, &devid, &audio_containerID, &fmt, &defperiod, &minperiod)) {
        wprintf(L"Found audio output!\n");
        wprintf(L"  Device ID: %S\n", devid);
        if (audio_containerID) {
            WCHAR wstr[39];
            StringFromGUID2(audio_containerID, wstr, 39*2);
            wprintf(L"  ContainerID: %S\n", wstr);
        }
        dump_fmt(fmt);
        if (fmt->nChannels != 4)
            wprintf(L"WARNING: audio device does not have 4 channels, this is going to cause issues\n");
    } else {
        wprintf(L"WARNING: audio output not found, haptics and speaker out won't work for Final Fantasy XIV Online and Final Fantasy VII Remake Intergrade\n");
    }

    if(containerID) {
        wprintf(L"\n\nSearching for audio output based on ContainerID (audio-based haptics in Ghostwire: Tokyo, Deathloop, ...)\n");
        if (find_audio_render_by(NULL, containerID, &devid, &audio_containerID, &fmt, &defperiod, &minperiod)) {
            wprintf(L"Found audio output!\n");
            wprintf(L"  Device ID: %S\n", devid);
            if (audio_containerID) {
                WCHAR wstr[39];
                StringFromGUID2(audio_containerID, wstr, 39*2);
                wprintf(L"  ContainerID: %S\n", wstr);
            }
            dump_fmt(fmt);
            if (fmt->nChannels != 4)
                wprintf(L"WARNING: audio device does not have 4 channels, this is going to cause issues\n");
        } else {
            wprintf(L"WARNING: audio output not found, haptics won't work for Ghostwire: Tokyo, Deathloop and other Wwise-based games\n");
        }
    } else {
        wprintf(L"\n\nWARNING: Cannot search for audio output based on ContainerID (audio-based haptics will not work in Ghostwire: Tokyo, Deathloop, ...)\n");
    }

    wprintf(L"\n\nWIP/debugging deathloop speaker access\n");
    deathloop_find_speaker_wip();

    return 0;
}

// InteractiveNotifications.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include "InteractiveNotifications.h"
#include <NotificationActivationCallback.h>

#include <Shellapi.h>
#include <string>
#include <iostream>
#include <SDKDDKVer.h>
#include <Windows.h>
#include <Psapi.h>
#include <strsafe.h>
#include <ShObjIdl.h>
#include <Shlobj.h>
#include <Pathcch.h>
#include <propvarutil.h>
#include <propkey.h>
#include <wchar.h>
#include <wrl.h>
#include <wrl\wrappers\corewrappers.h>
#include <windows.ui.notifications.h>


// Correct flow
// RegisterAppForNotificationSupport()
// RegisterActivator()

//  Name:     System.AppUserModel.ToastActivatorCLSID -- PKEY_AppUserModel_ToastActivatorCLSID
//  Type:     Guid -- VT_CLSID
//  FormatID: {9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3}, 26
//
//  Used to CoCreate an INotificationActivationCallback interface to notify about toast activations.
EXTERN_C const PROPERTYKEY DECLSPEC_SELECTANY PKEY_AppUserModel_ToastActivatorCLSID = { { 0x9F4C2855, 0x9F79, 0x4B39,{ 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } }, 26 };


using namespace ABI::Windows::Data::Xml::Dom;
using namespace ABI::Windows::UI::Notifications;
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;

struct CoTaskMemStringTraits
{
  typedef PWSTR Type;

  inline static bool Close(_In_ Type h) throw() { ::CoTaskMemFree(h); return true; }

  inline static Type GetInvalidValue() throw() { return nullptr; }
};
typedef HandleT<CoTaskMemStringTraits> CoTaskMemString;

const wchar_t Shortcut[] = LR"(Microsoft\Windows\Start Menu\Programs\B-hive Communicator.lnk)";

#define __CSID "B23D2B18-8DD7-403A-B9B7-152B40A1478C"
#define __PROTOCOL "bvcom://"

// For the app to be activated from Action Center, it needs to provide a COM server to be called
// when the notification is activated.  The CLSID of the object needs to be registered with the
// OS via its shortcut so that it knows who to call later.
class DECLSPEC_UUID(__CSID) NotificationActivator WrlSealed WrlFinal
: public RuntimeClass < RuntimeClassFlags<ClassicCom>,
  INotificationActivationCallback >
{
public:
  virtual HRESULT STDMETHODCALLTYPE Activate(
    _In_ LPCWSTR appUserModelId,
    _In_ LPCWSTR invokedArgs,
    _In_reads_(dataCount) const NOTIFICATION_USER_INPUT_DATA* data,
    ULONG dataCount) override
  {
    std::string args;

    for (int i = 0; i < dataCount; i++) {
      LPCWSTR lvalue = data[i].Value;
      LPCWSTR lkey = data[i].Key;

      std::wstring wvalue(lvalue);
      std::wstring wkey(lkey);

      std::string value = InteractiveNotifications::ws2utf8hex(wvalue);
      std::string key = InteractiveNotifications::ws2utf8hex(wkey);

      args = args + "%22key%22:%22" + key + "%22";
      args = args + ",%22value%22:%22" + value + "%22";
    }

    std::wstring wToastArgs(invokedArgs);
    std::string toastArgs(wToastArgs.begin(), wToastArgs.end());
    std::string escapedToastArgs = "";

    for (char ch : toastArgs) {
      switch (ch) {
        case ' ': escapedToastArgs += "%20"; break;
        case '"': escapedToastArgs += "%22"; break;
        default: escapedToastArgs += ch; break;
      }
    }

    std::string cmd = escapedToastArgs + "&userData=[{" + args + "}]";

    // Append protocol if not already present
    if (cmd.find(__PROTOCOL) != 0) {
      cmd = __PROTOCOL + cmd;
    }

    std::wstring wCmd = InteractiveNotifications::s2ws(cmd);

    ShellExecuteW(NULL,
      TEXT("open"),
      wCmd.c_str(),
      NULL,
      NULL,
      SW_SHOWNORMAL);

    return HRESULT();
  }
};
CoCreatableClass(NotificationActivator);

namespace InteractiveNotifications
{
  INTERACTIVENOTIFICATIONS_API HRESULT RegisterAppForNotificationSupport(PCWSTR shortcut, PCWSTR appId)
  {
    CoTaskMemString appData;
    auto hr = ::SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, appData.GetAddressOf());

    if (SUCCEEDED(hr))
    {
      wchar_t shortcutPath[MAX_PATH];

      hr = ::PathCchCombine(shortcutPath, ARRAYSIZE(shortcutPath), appData.Get(), shortcut);

      if (SUCCEEDED(hr))
      {
        DWORD attributes = ::GetFileAttributes(shortcutPath);
        bool fileExists = attributes < 0xFFFFFFF;

        if (!fileExists)
        {
          // Todo: This is probably the wrong path bc Squirrel
          wchar_t exePath[MAX_PATH];
          DWORD charWritten = ::GetModuleFileName(nullptr, exePath, ARRAYSIZE(exePath));
          hr = charWritten > 0 ? S_OK : HRESULT_FROM_WIN32(::GetLastError());

          if (SUCCEEDED(hr))
          {
            hr = InstallShortcut(shortcutPath, exePath, appId);
            if (SUCCEEDED(hr))
            {
              hr = RegisterComServer();
            }
          }
        }
      }
    }

    return hr;
  }

  _Use_decl_annotations_
  INTERACTIVENOTIFICATIONS_API HRESULT InstallShortcut(PCWSTR shortcutPath, PCWSTR exePath, PCWSTR appId)
  {
    ComPtr<IShellLink> shellLink;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink));

    if (!SUCCEEDED(hr)) return hr;
    hr = shellLink->SetPath(exePath);

    if (!SUCCEEDED(hr)) return hr;
    ComPtr<IPropertyStore> propertyStore;
    hr = shellLink.As(&propertyStore);

    if (!SUCCEEDED(hr)) return hr;
    PROPVARIANT propVar;
    propVar.vt = VT_LPWSTR;
    propVar.pwszVal = const_cast<PWSTR>(appId); // for _In_ scenarios, we don't need a copy
    hr = propertyStore->SetValue(PKEY_AppUserModel_ID, propVar);

    if (!SUCCEEDED(hr)) return hr;
    propVar.vt = VT_CLSID;
    propVar.puuid = const_cast<CLSID*>(&__uuidof(NotificationActivator));
    hr = propertyStore->SetValue(PKEY_AppUserModel_ToastActivatorCLSID, propVar);

    if (!SUCCEEDED(hr)) return hr;
    hr = propertyStore->Commit();

    if (!SUCCEEDED(hr)) return hr;
    ComPtr<IPersistFile> persistFile;
    hr = shellLink.As(&persistFile);

    if (!SUCCEEDED(hr)) return hr;
    hr = persistFile->Save(shortcutPath, TRUE);

    return hr;
  }

  _Use_decl_annotations_
  INTERACTIVENOTIFICATIONS_API HRESULT RegisterComServer()
  {
    wchar_t exePath[MAX_PATH];
    DWORD charWritten = ::GetModuleFileName(nullptr, exePath, ARRAYSIZE(exePath));
    auto hr = charWritten > 0 ? S_OK : HRESULT_FROM_WIN32(::GetLastError());

    if (SUCCEEDED(hr))
    {
      // We don't need to worry about overflow here as ::GetModuleFileName won't
      // return anything bigger than the max file system path (much fewer than max of DWORD).
      DWORD dataSize = static_cast<DWORD>((::wcslen(exePath) + 1) * sizeof(WCHAR));
      auto key = LR"(SOFTWARE\Classes\CLSID\{B23D2B18-8DD7-403A-B9B7-152B40A1478C}\LocalServer32)";

      return HRESULT_FROM_WIN32(::RegSetKeyValue(
        HKEY_CURRENT_USER,
        key,
        nullptr,
        REG_SZ,
        reinterpret_cast<const BYTE*>(exePath),
        dataSize));
    }

    return hr;
  }

  _Use_decl_annotations_
  INTERACTIVENOTIFICATIONS_API HRESULT RegisterActivator()
  {
    // Module<OutOfProc> needs a callback registered before it can be used.
    // Since we don't care about when it shuts down, we'll pass an empty lambda here.
    // If we need to clean up, do it here (we probably don't have to)
    Module<OutOfProc>::Create([] {});

    // If a local server process only hosts the COM object then COM expects
    // the COM server host to shutdown when the references drop to zero.
    // Since the user might still be using the program after activating the notification,
    // we don't want to shutdown immediately.  Incrementing the object count tells COM that
    // we aren't done yet.
    Module<OutOfProc>::GetModule().IncrementObjectCount();

    return Module<OutOfProc>::GetModule().RegisterObjects();
  }

  _Use_decl_annotations_
  INTERACTIVENOTIFICATIONS_API void UnregisterActivator()
  {
    Module<OutOfProc>::GetModule().UnregisterObjects();
    Module<OutOfProc>::GetModule().DecrementObjectCount();
  }

  INTERACTIVENOTIFICATIONS_API std::string ws2utf8hex(const std::wstring &input) {
    std::string output;
    int cbNeeded = WideCharToMultiByte(CP_UTF8, 0, input.c_str(), -1, NULL, 0, NULL, NULL);

    if (cbNeeded > 0) {
      char *utf8 = new char[cbNeeded];
      if (WideCharToMultiByte(CP_UTF8, 0, input.c_str(), -1, utf8, cbNeeded, NULL, NULL) != 0) {
        for (char *p = utf8; *p; *p++) {
          char onehex[5];
          _snprintf_s(onehex, sizeof(onehex), "%%%02.2X", (unsigned char)*p);
          output.append(onehex);
        }
      }
      delete[] utf8;
    }

    return output;
  }

  INTERACTIVENOTIFICATIONS_API std::wstring s2ws(const std::string& s)
  {
    int len;
    int slength = (int)s.length() + 1;

    len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, 0, 0);

    wchar_t* buf = new wchar_t[len];

    MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, buf, len);
    std::wstring r(buf);
    delete[] buf;

    return r;
  }
}

extern "C"
{
  __declspec(dllexport) void CRegisterForNotificationSupport(PCWSTR shortcut, PCWSTR appId)
  {
    InteractiveNotifications::RegisterAppForNotificationSupport(shortcut, appId);
  }

  __declspec(dllexport) void CRegisterActivator()
  {
    InteractiveNotifications::RegisterActivator();
  }

  __declspec(dllexport) void CUnregisterActivator()
  {
    InteractiveNotifications::UnregisterActivator();
  }
}
/*
 * Copyright 2004-2020 Sandboxie Holdings, LLC 
 * Copyright 2020 David Xanatos, xanasoft.com
 *
 * This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

//---------------------------------------------------------------------------
// Driver Assistant, start driver
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// Imported Functions
//---------------------------------------------------------------------------


extern "C" {

NTSTATUS LsaConnectUntrusted(PHANDLE LsaHandle);

NTSTATUS LsaDeregisterLogonProcess(HANDLE LsaHandle);

NTSTATUS LsaLookupAuthenticationPackage(
  HANDLE LsaHandle, ANSI_STRING *PackageName, PULONG AuthenticationPackage);

}


//---------------------------------------------------------------------------
// StartDriverAsync
//---------------------------------------------------------------------------


ULONG DriverAssist::StartDriverAsync(void *arg)
{
    DriverAssist* This = (DriverAssist*)arg;

    //
    // get windows version
    //

    OSVERSIONINFO osvi;
    memzero(&osvi, sizeof(osvi));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    GetVersionEx(&osvi);

    //
    // start the driver, but only if it isn't already active
    //

    bool ok = false;

    LONG rc = SbieApi_GetVersionEx(NULL, NULL);
    if (rc == 0) {
        ok = true;
        goto driver_started;
    }

    //
    // otherwise, try to start it
    //

    UNICODE_STRING uni;
    RtlInitUnicodeString(&uni,
        L"\\Registry\\Machine\\System\\CurrentControlSet"
        L"\\Services\\" SBIEDRV);

    rc = NtLoadDriver(&uni);
    if (rc == 0 || rc == STATUS_IMAGE_ALREADY_LOADED) {
        ok = true;
        goto driver_started;
    }

    if (rc != STATUS_PRIVILEGE_NOT_HELD || rc == STATUS_ACCESS_DENIED) {
        LogEvent(MSG_9234, 0x9153, rc);
        goto driver_started;
    }

    //
    // we have to enable a privilege to load the driver
    //

    WCHAR priv_space[64];
    TOKEN_PRIVILEGES *privs = (TOKEN_PRIVILEGES *)priv_space;
    HANDLE hToken;

    BOOL b = LookupPrivilegeValue(
                L"", SE_LOAD_DRIVER_NAME, &privs->Privileges[0].Luid);
    if (b) {

        privs->Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        privs->PrivilegeCount = 1;

        b = OpenProcessToken(
                GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken);
        if (b) {

            b = AdjustTokenPrivileges(hToken, FALSE, privs, 0, NULL, NULL);
            CloseHandle(hToken);
        }
    }

    rc = NtLoadDriver(&uni);
    if (rc == 0 || rc == STATUS_IMAGE_ALREADY_LOADED)
        ok = true;
    else
        LogEvent(MSG_9234, 0x9153, rc);

    //
    // the driver has been started (or was started already), check
    // version number before we continue the initialization
    //

driver_started:

    if (ok) {
        ULONG drv_abi_ver = 0;

        for (ULONG retries = 0; retries < 20; ++retries) {

            rc = SbieApi_GetVersionEx(NULL, &drv_abi_ver);
            if (rc == 0)
                break;
            Sleep(500);
        }

        if (drv_abi_ver != MY_ABI_VERSION) {
            LogEvent(MSG_9234, 0x9154, 0);
            ok = false;
        }
    }

    //
    // version numbers match
    //

    if (ok) {
        
        const char BlockList0[] =
        "2687F3F7D9DBF317A05251E2ED3C3D0A" "\r\n"
        "6AF28A3722ADC9FDB0F4B298508DD9E6" "\r\n"
        "54F2DF068889C1789AE9D7E948618EB6" "\r\n"
        "665FBAD025408A5B9355230EBD3381DC" "\r\n"

        "622D831B13B70B7CFFEC12E3778BF740" "\r\n"
        "2FDEB3584ED4DA007C2A1D49CFFF1062" "\r\n"
        "413616148FA9D3793B0E9BA4A396D3EE" "\r\n"
        "CC4DCCD36A13097B4478ADEB0FEF00CD" "\r\n"
        "32DE2C3B8E8859B6ECB6FF98BDF8DB15"		;
    const unsigned char BlockListSig0[64] =  {
        0x18, 0xd5, 0x23, 0x7f, 0x59, 0x06, 0xaf, 0xfa, 
        0x39, 0x3c, 0x5b, 0xa3, 0x69, 0x6a, 0x6a, 0xe8, 
        0x91, 0x5d, 0xaf, 0x1f, 0x99, 0xc3, 0x19, 0x0c, 
        0xf7, 0x07, 0x41, 0x44, 0xe8, 0xe6, 0x2b, 0x9b, 
        0x99, 0x75, 0xfe, 0x3f, 0x31, 0xd5, 0x0e, 0xe8, 
        0x16, 0x79, 0x49, 0xa6, 0x1c, 0x4b, 0x42, 0x4a, 
        0xaa, 0x7f, 0x68, 0x61, 0x70, 0xaf, 0x23, 0xfb, 
        0x3d, 0x50, 0x26, 0x5b, 0xbc, 0xe5, 0x6a, 0x2d 
    };

        std::string BlockList;
        BlockList.resize(0x10000, 0); // 64 kb should be enough
        ULONG BlockListLen = 0;
        ULONG status = SbieApi_Call(API_GET_SECURE_PARAM, 5, L"CertBlockList", (ULONG_PTR)BlockList.c_str(), BlockList.size(), (ULONG_PTR)&BlockListLen, 1);
        //BlockList.resize(BlockListLen);
        if (status != 0) // error
            BlockListLen = 0;

        if (BlockListLen < sizeof(BlockList0) - 1)
        {
            SbieApi_Call(API_SET_SECURE_PARAM, 3, L"CertBlockList", BlockList0, sizeof(BlockList0) - 1);
            SbieApi_Call(API_SET_SECURE_PARAM, 3, L"CertBlockListSig", BlockListSig0, sizeof(BlockListSig0));
            //BlockListLen = sizeof(BlockList0) - 1;
            //BlockList = BlockList0;
        }
    }

    //
    // continue with driver/service init
    //

    if (ok) {
        rc = SbieApi_Call(API_SET_SERVICE_PORT, 1, (ULONG_PTR)m_instance->m_PortHandle);
        if (rc != 0) {
            LogEvent(MSG_9234, 0x9361, rc);
            ok = false;
        }
    }

    if (ok) {
        SbieDll_InjectLow_InitSyscalls(TRUE);
        if (rc != 0) {
            LogEvent(MSG_9234, 0x9362, rc);
            ok = false;
        }
    }

    if (ok) {

        if (osvi.dwMajorVersion >= 6) {

            InitClipboard();
        }

        rc = SbieApi_Call(API_INIT_GUI, 0);

        if (rc != 0) {
            LogEvent(MSG_9234, 0x9156, rc);
            ok = false;
        }
    }

#ifdef XP_SUPPORT
#ifndef _WIN64

    if (ok) {

        //
        // prior to Windows Vista, we need to query the number for
        // the MSV10 authentication package, and tell our driver
        //

        if (osvi.dwMajorVersion == 5) {

            HANDLE LsaHandle;
            ANSI_STRING AuthPkgName;
            ULONG AuthPkgNum;

            rc = LsaConnectUntrusted(&LsaHandle);
            if (rc == 0) {

                RtlInitString(&AuthPkgName,
                    (const UCHAR *)"MICROSOFT_AUTHENTICATION_PACKAGE_V1_0");
                rc = LsaLookupAuthenticationPackage(
                    LsaHandle, &AuthPkgName, &AuthPkgNum);

                if (rc == 0)
                    SbieApi_Call(API_SET_LSA_AUTH_PKG, 1, (ULONG_PTR)AuthPkgNum);

                LsaDeregisterLogonProcess(LsaHandle);
            }
        }
    }


#endif ! _WIN64
#endif

    if (ok) {

#ifdef _M_ARM64

        //
        // Sandboxie on ARM64 requires x86 applications NOT to use the CHPE binaries.
        // 
        // So when ever the service starts it uses the global xtajit config to disable the use of CHPE binaries,
        // for x86 processes and restores the original value on service shutdown.
        //
        // See comment in HookImageOptionsEx core/low/init.c for more details.
        //

        extern BOOLEAN DisableCHPE;
        DisableCHPE = SbieApi_QueryConfBool(NULL, L"DisableCHPE", TRUE);

        if (DisableCHPE) {
            HKEY hkey = NULL;
            LSTATUS rc = RegCreateKeyEx(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Wow64\\x86\\xtajit",
                0, NULL, 0, KEY_ALL_ACCESS, NULL, &hkey, NULL);
            if (rc == 0)
            {
                DWORD value;
                DWORD size = sizeof(value);
                rc = RegQueryValueEx(hkey, L"LoadCHPEBinaries_old", NULL, NULL, (BYTE*)&value, &size);
                if (rc != 0) { // only save the old value when its not already saved
                    rc = RegQueryValueEx(hkey, L"LoadCHPEBinaries", NULL, NULL, (BYTE*)&value, &size);
                    if (rc == 0)
                        RegSetValueEx(hkey, L"LoadCHPEBinaries_old", NULL, REG_DWORD, (BYTE*)&value, size);
                }

                value = 0;
                RegSetValueEx(hkey, L"LoadCHPEBinaries", NULL, REG_DWORD, (BYTE*)&value, sizeof(value));

                RegCloseKey(hkey);
            }
        }
#endif

        //
        // trigger manual invocation of LogMessage to collect any
        // messages that were logged while the driver was starting
        //

        m_instance->LogMessage(NULL);

        m_instance->m_DriverReady = true;

        //
        // check if there are boxes configured to be run in bSession0 
        // at system boot and run them on service start
        //

        WCHAR boxname[BOXNAME_COUNT];
        for (ULONG i = 0; ; ++i) {

            rc = SbieApi_QueryConfAsIs(
                NULL, L"StartSystemBox", i, boxname, (BOXNAME_COUNT - 2) * sizeof(WCHAR));
            if (rc != 0)
                break;

            SbieDll_RunStartExe(L"auto_run", boxname);
        }
    }

    if (! ok) {

        AbortServer();
    }

    return 0;
}


//---------------------------------------------------------------------------
// IsDriverReady
//---------------------------------------------------------------------------


bool DriverAssist::IsDriverReady()
{
    if (m_instance && m_instance->m_DriverReady)
        return true;
    else
        return false;
}


//---------------------------------------------------------------------------
// InitClipboard
//---------------------------------------------------------------------------


void DriverAssist::InitClipboard()
{
    //
    // on Windows Vista and later, we need to figure out the
    // structure of the internal clipboard item.  we put some
    // data on the clipboard, and let Gui_InitClipboard in file
    // core/drv/gui.c figure out the internal structure
    //

    HANDLE hGlobal1 = GlobalAlloc(GMEM_MOVEABLE, 8 * sizeof(WCHAR));
    HANDLE hGlobal2 = GlobalAlloc(GMEM_MOVEABLE, 8 * sizeof(WCHAR));

    if (hGlobal1 && hGlobal2) {

        WCHAR *pGlobal = (WCHAR *)GlobalLock(hGlobal1);
        *pGlobal = L'\0';
        GlobalUnlock(hGlobal1);
        pGlobal = (WCHAR *)GlobalLock(hGlobal2);
        *pGlobal = L'\0';
        GlobalUnlock(hGlobal2);

        for (int retry = 0; retry < 8 * (1000 / 250); ++retry) {

            if (OpenClipboard(NULL)) {

                EmptyClipboard();
                SetClipboardData(0x111111, hGlobal1);
                SetClipboardData(0x222222, hGlobal1);
                SetClipboardData(0x333333, hGlobal2);
                SetClipboardData(0x444444, hGlobal2);

                SbieApi_Call(API_GUI_CLIPBOARD, 1, (ULONG_PTR)-1);

                EmptyClipboard();
                CloseClipboard();

                break;

            } else
                Sleep(250);
        }
    }

    if (hGlobal1)
        GlobalFree(hGlobal1);

    if (hGlobal2)
        GlobalFree(hGlobal2);
}

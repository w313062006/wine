/*
 * Copyright 2017 Alistair Leslie-Hughes
 * Copyright 2019 Vijay Kiran Kamuju
 * Copyright 2021 Zebediah Figura for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdbool.h>
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include "initguid.h"
#include "wmsdk.h"
#include "wmsecure.h"
#include "amvideo.h"
#include "uuids.h"
#include "wmcodecdsp.h"

#include "wine/test.h"

HRESULT WINAPI WMCreateWriterPriv(IWMWriter **writer);

static BOOL compare_media_types(const WM_MEDIA_TYPE *a, const WM_MEDIA_TYPE *b)
{
    /* We can't use memcmp(), because WM_MEDIA_TYPE has a hole, which sometimes
     * contains junk. */

    return IsEqualGUID(&a->majortype, &b->majortype)
            && IsEqualGUID(&a->subtype, &b->subtype)
            && a->bFixedSizeSamples == b->bFixedSizeSamples
            && a->bTemporalCompression == b->bTemporalCompression
            && a->lSampleSize == b->lSampleSize
            && IsEqualGUID(&a->formattype, &b->formattype)
            && a->pUnk == b->pUnk
            && a->cbFormat == b->cbFormat
            && !memcmp(a->pbFormat, b->pbFormat, a->cbFormat);
}

static WCHAR *load_resource(const WCHAR *name)
{
    static WCHAR pathW[MAX_PATH];
    DWORD written;
    HANDLE file;
    HRSRC res;
    void *ptr;

    GetTempPathW(ARRAY_SIZE(pathW), pathW);
    wcscat(pathW, name);

    file = CreateFileW(pathW, GENERIC_READ|GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, 0);
    ok(file != INVALID_HANDLE_VALUE, "Failed to create file %s, error %u.\n",
            wine_dbgstr_w(pathW), GetLastError());

    res = FindResourceW(NULL, name, (LPCWSTR)RT_RCDATA);
    ok(!!res, "Failed to load resource, error %u.\n", GetLastError());
    ptr = LockResource(LoadResource(GetModuleHandleA(NULL), res));
    WriteFile(file, ptr, SizeofResource( GetModuleHandleA(NULL), res), &written, NULL);
    ok(written == SizeofResource(GetModuleHandleA(NULL), res), "Failed to write resource.\n");
    CloseHandle(file);

    return pathW;
}

#define check_interface(a, b, c) check_interface_(__LINE__, a, b, c)
static HRESULT check_interface_(unsigned int line, void *iface, REFIID riid, BOOL supported)
{
    HRESULT hr, expected_hr;
    IUnknown *unknown = iface, *out;

    expected_hr = supported ? S_OK : E_NOINTERFACE;

    hr = IUnknown_QueryInterface(unknown, riid, (void **)&out);
    ok_(__FILE__, line)(hr == expected_hr, "Got hr %#x, expected %#x.\n", hr, expected_hr);
    if (SUCCEEDED(hr))
        IUnknown_Release(out);
    return hr;
}

static void test_wmwriter_interfaces(void)
{
    HRESULT hr;
    IWMWriter *writer;

    hr = WMCreateWriter( NULL, &writer );
    ok(hr == S_OK, "WMCreateWriter failed 0x%08x\n", hr);
    if(FAILED(hr))
    {
        win_skip("Failed to create IWMWriter\n");
        return;
    }

    check_interface(writer, &IID_IWMWriterSink, FALSE);

    check_interface(writer, &IID_IWMWriter, TRUE);
    check_interface(writer, &IID_IWMWriterAdvanced, TRUE);
    check_interface(writer, &IID_IWMWriterAdvanced2, TRUE);
    check_interface(writer, &IID_IWMWriterAdvanced3, TRUE);
    todo_wine check_interface(writer, &IID_IWMWriterPreprocess, TRUE);
    todo_wine check_interface(writer, &IID_IWMHeaderInfo, TRUE);
    todo_wine check_interface(writer, &IID_IWMHeaderInfo2, TRUE);
    todo_wine check_interface(writer, &IID_IWMHeaderInfo3, TRUE);

    IWMWriter_Release(writer);
}

static void test_wmreader_interfaces(void)
{
    HRESULT hr;
    IWMReader *reader;

    hr = WMCreateReader( NULL, 0, &reader );
    ok(hr == S_OK, "WMCreateReader failed 0x%08x\n", hr);
    if(FAILED(hr))
    {
        win_skip("Failed to create IWMReader\n");
        return;
    }

    check_interface(reader, &IID_IWMDRMReader, FALSE);
    check_interface(reader, &IID_IWMDRMReader2, FALSE);
    check_interface(reader, &IID_IWMDRMReader3, FALSE);
    check_interface(reader, &IID_IWMSyncReader, FALSE);
    check_interface(reader, &IID_IWMSyncReader2, FALSE);

    check_interface(reader, &IID_IReferenceClock, TRUE);
    check_interface(reader, &IID_IWMHeaderInfo, TRUE);
    check_interface(reader, &IID_IWMHeaderInfo2, TRUE);
    check_interface(reader, &IID_IWMHeaderInfo3, TRUE);
    check_interface(reader, &IID_IWMLanguageList, TRUE);
    check_interface(reader, &IID_IWMPacketSize, TRUE);
    check_interface(reader, &IID_IWMPacketSize2, TRUE);
    check_interface(reader, &IID_IWMProfile, TRUE);
    check_interface(reader, &IID_IWMProfile2, TRUE);
    check_interface(reader, &IID_IWMProfile3, TRUE);
    check_interface(reader, &IID_IWMReader, TRUE);
    check_interface(reader, &IID_IWMReaderAccelerator, TRUE);
    check_interface(reader, &IID_IWMReaderAdvanced, TRUE);
    check_interface(reader, &IID_IWMReaderAdvanced2, TRUE);
    check_interface(reader, &IID_IWMReaderAdvanced3, TRUE);
    check_interface(reader, &IID_IWMReaderAdvanced4, TRUE);
    check_interface(reader, &IID_IWMReaderAdvanced5, TRUE);
    check_interface(reader, &IID_IWMReaderAdvanced6, TRUE);
    check_interface(reader, &IID_IWMReaderNetworkConfig, TRUE);
    check_interface(reader, &IID_IWMReaderNetworkConfig2, TRUE);
    check_interface(reader, &IID_IWMReaderPlaylistBurn, TRUE);
    check_interface(reader, &IID_IWMReaderStreamClock, TRUE);
    check_interface(reader, &IID_IWMReaderTimecode, TRUE);
    check_interface(reader, &IID_IWMReaderTypeNegotiation, TRUE);

    IWMReader_Release(reader);
}

static void test_wmsyncreader_interfaces(void)
{
    HRESULT hr;
    IWMSyncReader *reader;

    hr = WMCreateSyncReader( NULL, 0, &reader );
    ok(hr == S_OK, "WMCreateSyncReader failed 0x%08x\n", hr);
    if(FAILED(hr))
    {
        win_skip("Failed to create IWMSyncReader\n");
        return;
    }

    check_interface(reader, &IID_IReferenceClock, FALSE);
    check_interface(reader, &IID_IWMDRMReader, FALSE);
    check_interface(reader, &IID_IWMDRMReader2, FALSE);
    check_interface(reader, &IID_IWMDRMReader3, FALSE);
    check_interface(reader, &IID_IWMReader, FALSE);
    check_interface(reader, &IID_IWMReaderAccelerator, FALSE);
    check_interface(reader, &IID_IWMReaderAdvanced, FALSE);
    check_interface(reader, &IID_IWMReaderAdvanced2, FALSE);
    check_interface(reader, &IID_IWMReaderAdvanced3, FALSE);
    check_interface(reader, &IID_IWMReaderAdvanced4, FALSE);
    check_interface(reader, &IID_IWMReaderAdvanced5, FALSE);
    check_interface(reader, &IID_IWMReaderAdvanced6, FALSE);
    check_interface(reader, &IID_IWMReaderNetworkConfig, FALSE);
    check_interface(reader, &IID_IWMReaderNetworkConfig2, FALSE);
    check_interface(reader, &IID_IWMReaderStreamClock, FALSE);
    check_interface(reader, &IID_IWMReaderTypeNegotiation, FALSE);

    check_interface(reader, &IID_IWMHeaderInfo, TRUE);
    check_interface(reader, &IID_IWMHeaderInfo2, TRUE);
    check_interface(reader, &IID_IWMHeaderInfo3, TRUE);
    check_interface(reader, &IID_IWMLanguageList, TRUE);
    check_interface(reader, &IID_IWMPacketSize, TRUE);
    check_interface(reader, &IID_IWMPacketSize2, TRUE);
    check_interface(reader, &IID_IWMProfile, TRUE);
    check_interface(reader, &IID_IWMProfile2, TRUE);
    check_interface(reader, &IID_IWMProfile3, TRUE);
    check_interface(reader, &IID_IWMReaderPlaylistBurn, TRUE);
    check_interface(reader, &IID_IWMReaderTimecode, TRUE);
    check_interface(reader, &IID_IWMSyncReader, TRUE);
    check_interface(reader, &IID_IWMSyncReader2, TRUE);

    IWMSyncReader_Release(reader);
}


static void test_profile_manager_interfaces(void)
{
    HRESULT hr;
    IWMProfileManager  *profile;

    hr = WMCreateProfileManager(&profile);
    ok(hr == S_OK, "WMCreateProfileManager failed 0x%08x\n", hr);
    if(FAILED(hr))
    {
        win_skip("Failed to create IWMProfileManager\n");
        return;
    }

    IWMProfileManager_Release(profile);
}

static void test_WMCreateWriterPriv(void)
{
    IWMWriter *writer, *writer2;
    HRESULT hr;

    hr = WMCreateWriterPriv(&writer);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IWMWriter_QueryInterface(writer, &IID_IWMWriter, (void**)&writer2);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    IWMWriter_Release(writer);
    IWMWriter_Release(writer2);
}

static void test_urlextension(void)
{
    HRESULT hr;

    hr = WMCheckURLExtension(NULL);
    ok(hr == E_INVALIDARG, "WMCheckURLExtension failed 0x%08x\n", hr);
    hr = WMCheckURLExtension(L"test.mkv");
    ok(hr == NS_E_INVALID_NAME, "WMCheckURLExtension failed 0x%08x\n", hr);
    hr = WMCheckURLExtension(L"test.mp3");
    todo_wine ok(hr == S_OK, "WMCheckURLExtension failed 0x%08x\n", hr);
    hr = WMCheckURLExtension(L"abcd://test/test.wmv");
    todo_wine ok(hr == S_OK, "WMCheckURLExtension failed 0x%08x\n", hr);
    hr = WMCheckURLExtension(L"http://test/t.asf?alt=t.mkv");
    todo_wine ok(hr == S_OK, "WMCheckURLExtension failed 0x%08x\n", hr);
}

static void test_iscontentprotected(void)
{
    HRESULT hr;
    BOOL drm;

    hr = WMIsContentProtected(NULL, NULL);
    ok(hr == E_INVALIDARG, "WMIsContentProtected failed 0x%08x\n", hr);
    hr = WMIsContentProtected(NULL, &drm);
    ok(hr == E_INVALIDARG, "WMIsContentProtected failed 0x%08x\n", hr);
    hr = WMIsContentProtected(L"test.mp3", NULL);
    ok(hr == E_INVALIDARG, "WMIsContentProtected failed 0x%08x\n", hr);
    hr = WMIsContentProtected(L"test.mp3", &drm);
    ok(hr == S_FALSE, "WMIsContentProtected failed 0x%08x\n", hr);
    ok(drm == FALSE, "got %0dx\n", drm);
}

struct teststream
{
    IStream IStream_iface;
    LONG refcount;
    HANDLE file;
};

static struct teststream *impl_from_IStream(IStream *iface)
{
    return CONTAINING_RECORD(iface, struct teststream, IStream_iface);
}

static HRESULT WINAPI stream_QueryInterface(IStream *iface, REFIID iid, void **out)
{
    if (winetest_debug > 1)
        trace("%04x: IStream::QueryInterface(%s)\n", GetCurrentThreadId(), debugstr_guid(iid));

    if (!IsEqualGUID(iid, &IID_IWMGetSecureChannel) && !IsEqualGUID(iid, &IID_IWMIStreamProps))
        ok(0, "Unexpected IID %s.\n", debugstr_guid(iid));

    return E_NOINTERFACE;
}

static ULONG WINAPI stream_AddRef(IStream *iface)
{
    struct teststream *stream = impl_from_IStream(iface);

    return InterlockedIncrement(&stream->refcount);
}

static ULONG WINAPI stream_Release(IStream *iface)
{
    struct teststream *stream = impl_from_IStream(iface);

    return InterlockedDecrement(&stream->refcount);
}

static HRESULT WINAPI stream_Read(IStream *iface, void *data, ULONG size, ULONG *ret_size)
{
    struct teststream *stream = impl_from_IStream(iface);

    if (winetest_debug > 2)
        trace("%04x: IStream::Read(size %u)\n", GetCurrentThreadId(), size);

    ok(size > 0, "Got zero size.\n");
    ok(!!ret_size, "Got NULL ret_size pointer.\n");
    if (!ReadFile(stream->file, data, size, ret_size, NULL))
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

static HRESULT WINAPI stream_Write(IStream *iface, const void *data, ULONG size, ULONG *ret_size)
{
    ok(0, "Unexpected call.\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI stream_Seek(IStream *iface, LARGE_INTEGER offset, DWORD method, ULARGE_INTEGER *ret_offset)
{
    struct teststream *stream = impl_from_IStream(iface);
    LARGE_INTEGER size;

    if (winetest_debug > 2)
        trace("%04x: IStream::Seek(offset %I64u, method %#x)\n", GetCurrentThreadId(), offset.QuadPart, method);

    GetFileSizeEx(stream->file, &size);
    ok(offset.QuadPart < size.QuadPart, "Expected offset less than size %I64u, got %I64u.\n",
            size.QuadPart, offset.QuadPart);

    ok(method == STREAM_SEEK_SET, "Got method %#x.\n", method);
    ok(!ret_offset, "Got unexpected ret_offset pointer %p\n", ret_offset);

    if (!SetFilePointerEx(stream->file, offset, &offset, method))
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

static HRESULT WINAPI stream_SetSize(IStream *iface, ULARGE_INTEGER size)
{
    ok(0, "Unexpected call.\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI stream_CopyTo(IStream *iface, IStream *dest, ULARGE_INTEGER size,
        ULARGE_INTEGER *read_size, ULARGE_INTEGER *write_size)
{
    ok(0, "Unexpected call.\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI stream_Commit(IStream *iface, DWORD flags)
{
    ok(0, "Unexpected call.\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI stream_Revert(IStream *iface)
{
    ok(0, "Unexpected call.\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI stream_LockRegion(IStream *iface, ULARGE_INTEGER offset, ULARGE_INTEGER size, DWORD type)
{
    ok(0, "Unexpected call.\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI stream_UnlockRegion(IStream *iface, ULARGE_INTEGER offset, ULARGE_INTEGER size, DWORD type)
{
    ok(0, "Unexpected call.\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI stream_Stat(IStream *iface, STATSTG *stat, DWORD flags)
{
    struct teststream *stream = impl_from_IStream(iface);
    LARGE_INTEGER size;

    if (winetest_debug > 1)
        trace("%04x: IStream::Stat(flags %#x)\n", GetCurrentThreadId(), flags);

    ok(flags == STATFLAG_NONAME, "Got flags %#x.\n", flags);

    stat->type = 0xdeadbeef;
    GetFileSizeEx(stream->file, &size);
    stat->cbSize.QuadPart = size.QuadPart;
    stat->grfMode = 0;
    stat->grfLocksSupported = TRUE;

    return S_OK;
}

static HRESULT WINAPI stream_Clone(IStream *iface, IStream **out)
{
    ok(0, "Unexpected call.\n");
    return E_NOTIMPL;
}

static const IStreamVtbl stream_vtbl =
{
    stream_QueryInterface,
    stream_AddRef,
    stream_Release,
    stream_Read,
    stream_Write,
    stream_Seek,
    stream_SetSize,
    stream_CopyTo,
    stream_Commit,
    stream_Revert,
    stream_LockRegion,
    stream_UnlockRegion,
    stream_Stat,
    stream_Clone,
};

static void teststream_init(struct teststream *stream, HANDLE file)
{
    memset(stream, 0, sizeof(*stream));
    stream->IStream_iface.lpVtbl = &stream_vtbl;
    stream->refcount = 1;
    stream->file = file;
}

static void test_reader_attributes(IWMProfile *profile)
{
    WORD size, stream_number, ret_stream_number;
    IWMHeaderInfo *header_info;
    IWMStreamConfig *config;
    WMT_ATTR_DATATYPE type;
    ULONG count, i;
    QWORD duration;
    DWORD dword;
    HRESULT hr;

    IWMProfile_QueryInterface(profile, &IID_IWMHeaderInfo, (void **)&header_info);

    hr = IWMProfile_GetStreamCount(profile, &count);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(count == 2, "Got count %u.\n", count);

    for (i = 0; i < count; ++i)
    {
        hr = IWMProfile_GetStream(profile, i, &config);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        hr = IWMStreamConfig_GetStreamNumber(config, &stream_number);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        ret_stream_number = stream_number;

        size = sizeof(DWORD);
        type = 0xdeadbeef;
        dword = 0xdeadbeef;
        hr = IWMHeaderInfo_GetAttributeByName(header_info, &ret_stream_number,
                L"WM/VideoFrameRate", &type, (BYTE *)&dword, &size);
        ok(hr == ASF_E_NOTFOUND, "Got hr %#x.\n", hr);
        ok(type == 0xdeadbeef, "Got type %#x.\n", type);
        ok(size == sizeof(DWORD), "Got size %u.\n", size);
        ok(dword == 0xdeadbeef, "Got frame rate %u.\n", dword);
        ok(ret_stream_number == stream_number, "Expected stream number %u, got %u.\n",
                stream_number, ret_stream_number);

        size = sizeof(QWORD);
        type = 0xdeadbeef;
        duration = 0xdeadbeef;
        hr = IWMHeaderInfo_GetAttributeByName(header_info, &ret_stream_number,
                L"Duration", &type, (BYTE *)&duration, &size);
        ok(hr == ASF_E_NOTFOUND, "Got hr %#x.\n", hr);
        ok(type == 0xdeadbeef, "Got type %#x.\n", type);
        ok(size == sizeof(QWORD), "Got size %u.\n", size);
        ok(ret_stream_number == stream_number, "Expected stream number %u, got %u.\n",
                stream_number, ret_stream_number);

        size = sizeof(DWORD);
        type = 0xdeadbeef;
        dword = 0xdeadbeef;
        hr = IWMHeaderInfo_GetAttributeByName(header_info, &ret_stream_number,
                L"Seekable", &type, (BYTE *)&dword, &size);
        ok(hr == ASF_E_NOTFOUND, "Got hr %#x.\n", hr);
        ok(type == 0xdeadbeef, "Got type %#x.\n", type);
        ok(size == sizeof(DWORD), "Got size %u.\n", size);
        ok(ret_stream_number == stream_number, "Expected stream number %u, got %u.\n",
                stream_number, ret_stream_number);

        IWMStreamConfig_Release(config);
    }

    /* WM/VideoFrameRate with a NULL stream number. */

    size = sizeof(DWORD);
    type = 0xdeadbeef;
    dword = 0xdeadbeef;
    hr = IWMHeaderInfo_GetAttributeByName(header_info, NULL,
            L"WM/VideoFrameRate", &type, (BYTE *)&dword, &size);
    ok(hr == E_INVALIDARG, "Got hr %#x.\n", hr);
    ok(type == 0xdeadbeef, "Got type %#x.\n", type);
    ok(size == sizeof(DWORD), "Got size %u.\n", size);
    ok(dword == 0xdeadbeef, "Got frame rate %u.\n", dword);

    /* And with a zero stream number. */

    stream_number = 0;
    size = sizeof(DWORD);
    type = 0xdeadbeef;
    dword = 0xdeadbeef;
    hr = IWMHeaderInfo_GetAttributeByName(header_info, &stream_number,
            L"WM/VideoFrameRate", &type, (BYTE *)&dword, &size);
    ok(hr == ASF_E_NOTFOUND, "Got hr %#x.\n", hr);
    ok(type == 0xdeadbeef, "Got type %#x.\n", type);
    ok(size == sizeof(DWORD), "Got size %u.\n", size);
    ok(dword == 0xdeadbeef, "Got frame rate %u.\n", dword);
    ok(stream_number == 0, "Got stream number %u.\n", stream_number);

    /* Duration with a NULL stream number. */

    size = sizeof(QWORD);
    type = 0xdeadbeef;
    duration = 0xdeadbeef;
    hr = IWMHeaderInfo_GetAttributeByName(header_info, NULL,
            L"Duration", &type, (BYTE *)&duration, &size);
    ok(hr == E_INVALIDARG, "Got hr %#x.\n", hr);
    ok(type == 0xdeadbeef, "Got type %#x.\n", type);
    ok(size == sizeof(QWORD), "Got size %u.\n", size);
    ok(duration == 0xdeadbeef, "Got duration %I64u.\n", duration);

    /* And with a zero stream number. */

    size = sizeof(QWORD);
    type = 0xdeadbeef;
    duration = 0xdeadbeef;
    hr = IWMHeaderInfo_GetAttributeByName(header_info, &stream_number,
            L"Duration", &type, (BYTE *)&duration, &size);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(type == WMT_TYPE_QWORD, "Got type %#x.\n", type);
    ok(size == sizeof(QWORD), "Got size %u.\n", size);
    ok(duration == 20460000, "Got duration %I64u.\n", duration);
    ok(stream_number == 0, "Got stream number %u.\n", stream_number);

    /* Pass a too-small size. */

    size = sizeof(QWORD) - 1;
    type = 0xdeadbeef;
    duration = 0xdeadbeef;
    hr = IWMHeaderInfo_GetAttributeByName(header_info, &stream_number,
            L"Duration", &type, (BYTE *)&duration, &size);
    ok(hr == ASF_E_BUFFERTOOSMALL, "Got hr %#x.\n", hr);
    ok(type == 0xdeadbeef, "Got type %#x.\n", type);
    ok(size == sizeof(QWORD), "Got size %u.\n", size);
    ok(duration == 0xdeadbeef, "Got duration %I64u.\n", duration);
    ok(stream_number == 0, "Got stream number %u.\n", stream_number);

    /* Pass a NULL buffer. */

    size = 0xdead;
    type = 0xdeadbeef;
    hr = IWMHeaderInfo_GetAttributeByName(header_info, &stream_number,
            L"Duration", &type, NULL, &size);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(type == WMT_TYPE_QWORD, "Got type %#x.\n", type);
    ok(size == sizeof(QWORD), "Got size %u.\n", size);
    ok(stream_number == 0, "Got stream number %u.\n", stream_number);

    size = sizeof(DWORD);
    type = 0xdeadbeef;
    dword = 0xdeadbeef;
    hr = IWMHeaderInfo_GetAttributeByName(header_info, &stream_number,
            L"Seekable", &type, (BYTE *)&dword, &size);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(type == WMT_TYPE_BOOL, "Got type %#x.\n", type);
    ok(size == sizeof(DWORD), "Got size %u.\n", size);
    ok(dword == TRUE, "Got duration %I64u.\n", duration);
    ok(stream_number == 0, "Got stream number %u.\n", stream_number);

    IWMHeaderInfo_Release(header_info);
}

static void test_sync_reader_streaming(void)
{
    DWORD size, flags, output_number, expect_output_number;
    const WCHAR *filename = load_resource(L"test.wmv");
    WORD stream_numbers[2], stream_number;
    IWMStreamConfig *config, *config2;
    bool eos[2] = {0}, first = true;
    struct teststream stream;
    ULONG i, j, count, ref;
    IWMSyncReader *reader;
    IWMProfile *profile;
    QWORD pts, duration;
    INSSBuffer *sample;
    HANDLE file;
    HRESULT hr;
    BYTE *data;
    BOOL ret;

    file = CreateFileW(filename, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, 0);
    ok(file != INVALID_HANDLE_VALUE, "Failed to open %s, error %u.\n", debugstr_w(file), GetLastError());

    teststream_init(&stream, file);

    hr = WMCreateSyncReader(NULL, 0, &reader);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    IWMSyncReader_QueryInterface(reader, &IID_IWMProfile, (void **)&profile);

    hr = IWMSyncReader_OpenStream(reader, &stream.IStream_iface);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(stream.refcount > 1, "Got refcount %d.\n", stream.refcount);

    hr = IWMProfile_GetStreamCount(profile, NULL);
    ok(hr == E_INVALIDARG, "Got hr %#x.\n", hr);

    count = 0xdeadbeef;
    hr = IWMProfile_GetStreamCount(profile, &count);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(count == 2, "Got count %u.\n", count);

    count = 0xdeadbeef;
    hr = IWMSyncReader_GetOutputCount(reader, &count);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(count == 2, "Got count %u.\n", count);

    for (i = 0; i < 2; ++i)
    {
        hr = IWMProfile_GetStream(profile, i, &config);
        ok(hr == S_OK, "Got hr %#x.\n", hr);

        hr = IWMProfile_GetStream(profile, i, &config2);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        ok(config2 != config, "Expected different objects.\n");
        ref = IWMStreamConfig_Release(config2);
        ok(!ref, "Got outstanding refcount %d.\n", ref);

        stream_numbers[i] = 0xdead;
        hr = IWMStreamConfig_GetStreamNumber(config, &stream_numbers[i]);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        ok(stream_numbers[i] == i + 1, "Got stream number %u.\n", stream_numbers[i]);

        ref = IWMStreamConfig_Release(config);
        ok(!ref, "Got outstanding refcount %d.\n", ref);

        hr = IWMSyncReader_SetReadStreamSamples(reader, stream_numbers[i], FALSE);
        todo_wine ok(hr == S_OK, "Got hr %#x.\n", hr);
    }

    hr = IWMProfile_GetStream(profile, 2, &config);
    ok(hr == E_INVALIDARG, "Got hr %#x.\n", hr);

    while (!eos[0] || !eos[1])
    {
        for (j = 0; j < 2; ++j)
        {
            stream_number = pts = duration = flags = output_number = 0xdeadbeef;
            hr = IWMSyncReader_GetNextSample(reader, stream_numbers[j], &sample,
                    &pts, &duration, &flags, &output_number, &stream_number);
            if (first)
                ok(hr == S_OK, "Expected at least one valid sample; got hr %#x.\n", hr);
            else if (eos[j])
                ok(hr == NS_E_NO_MORE_SAMPLES, "Got hr %#x.\n", hr);
            else
                ok(hr == S_OK || hr == NS_E_NO_MORE_SAMPLES, "Got hr %#x.\n", hr);

            if (hr == S_OK)
            {
                hr = INSSBuffer_GetBufferAndLength(sample, &data, &size);
                ok(hr == S_OK, "Got hr %#x.\n", hr);
                ref = INSSBuffer_Release(sample);
                ok(!ref, "Got outstanding refcount %d.\n", ref);

                hr = IWMSyncReader_GetOutputNumberForStream(reader, stream_number, &expect_output_number);
                ok(hr == S_OK, "Got hr %#x.\n", hr);
                ok(output_number == expect_output_number, "Expected output number %u, got %u.\n",
                        expect_output_number, output_number);
            }
            else
            {
                ok(pts == 0xdeadbeef, "Got PTS %I64u.\n", pts);
                ok(duration == 0xdeadbeef, "Got duration %I64u.\n", duration);
                ok(flags == 0xdeadbeef, "Got flags %#x.\n", flags);
                ok(output_number == 0xdeadbeef, "Got output number %u.\n", output_number);
                eos[j] = true;
            }

            ok(stream_number == stream_numbers[j], "Expected stream number %u, got %u.\n",
                    stream_numbers[j], stream_number);
        }
        first = false;
    }

    hr = IWMSyncReader_GetNextSample(reader, stream_numbers[0], &sample,
            &pts, &duration, &flags, NULL, NULL);
    ok(hr == NS_E_NO_MORE_SAMPLES, "Got hr %#x.\n", hr);

    hr = IWMSyncReader_GetNextSample(reader, stream_numbers[1], &sample,
            &pts, &duration, &flags, NULL, NULL);
    ok(hr == NS_E_NO_MORE_SAMPLES, "Got hr %#x.\n", hr);

    hr = IWMSyncReader_SetRange(reader, 0, 0);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IWMSyncReader_GetNextSample(reader, stream_numbers[0], &sample, &pts, &duration, &flags, NULL, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    INSSBuffer_Release(sample);

    hr = IWMSyncReader_GetNextSample(reader, 0, &sample, &pts, &duration, &flags, NULL, NULL);
    ok(hr == E_INVALIDARG, "Got hr %#x.\n", hr);

    hr = IWMSyncReader_GetNextSample(reader, 0, &sample, &pts, &duration, &flags, &output_number, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    INSSBuffer_Release(sample);

    hr = IWMSyncReader_GetNextSample(reader, 0, &sample, &pts, &duration, &flags, NULL, &stream_number);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    INSSBuffer_Release(sample);

    for (;;)
    {
        stream_number = pts = duration = flags = output_number = 0xdeadbeef;
        hr = IWMSyncReader_GetNextSample(reader, 0, &sample,
                &pts, &duration, &flags, &output_number, &stream_number);
        ok(hr == S_OK || hr == NS_E_NO_MORE_SAMPLES, "Got hr %#x.\n", hr);

        if (hr == S_OK)
        {
            hr = INSSBuffer_GetBufferAndLength(sample, &data, &size);
            ok(hr == S_OK, "Got hr %#x.\n", hr);
            ref = INSSBuffer_Release(sample);
            ok(!ref, "Got outstanding refcount %d.\n", ref);
        }
        else
        {
            ok(pts == 0xdeadbeef, "Got PTS %I64u.\n", pts);
            ok(duration == 0xdeadbeef, "Got duration %I64u.\n", duration);
            ok(flags == 0xdeadbeef, "Got flags %#x.\n", flags);
            ok(output_number == 0xdeadbeef, "Got output number %u.\n", output_number);
            ok(stream_number == 0xbeef, "Got stream number %u.\n", stream_number);
            break;
        }
    }

    hr = IWMSyncReader_GetNextSample(reader, 0, &sample,
            &pts, &duration, &flags, NULL, &stream_number);
    ok(hr == NS_E_NO_MORE_SAMPLES, "Got hr %#x.\n", hr);

    hr = IWMSyncReader_GetNextSample(reader, stream_numbers[0], &sample,
            &pts, &duration, &flags, NULL, NULL);
    ok(hr == NS_E_NO_MORE_SAMPLES, "Got hr %#x.\n", hr);

    hr = IWMSyncReader_GetNextSample(reader, stream_numbers[1], &sample,
            &pts, &duration, &flags, NULL, NULL);
    ok(hr == NS_E_NO_MORE_SAMPLES, "Got hr %#x.\n", hr);

    test_reader_attributes(profile);

    hr = IWMSyncReader_Close(reader);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IWMSyncReader_Close(reader);
    ok(hr == NS_E_INVALID_REQUEST, "Got hr %#x.\n", hr);

    ok(stream.refcount == 1, "Got outstanding refcount %d.\n", stream.refcount);

    SetFilePointer(stream.file, 0, NULL, FILE_BEGIN);
    hr = IWMSyncReader_OpenStream(reader, &stream.IStream_iface);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(stream.refcount > 1, "Got refcount %d.\n", stream.refcount);

    IWMProfile_Release(profile);
    ref = IWMSyncReader_Release(reader);
    ok(!ref, "Got outstanding refcount %d.\n", ref);

    ok(stream.refcount == 1, "Got outstanding refcount %d.\n", stream.refcount);
    CloseHandle(stream.file);
    ret = DeleteFileW(filename);
    ok(ret, "Failed to delete %s, error %u.\n", debugstr_w(filename), GetLastError());
}

static void check_video_type(const WM_MEDIA_TYPE *mt)
{
    const VIDEOINFOHEADER *video_info = (const VIDEOINFOHEADER *)mt->pbFormat;
    static const RECT rect = {.right = 64, .bottom = 48};

    ok(IsEqualGUID(&mt->formattype, &FORMAT_VideoInfo), "Got format %s.\n", debugstr_guid(&mt->formattype));
    ok(mt->bFixedSizeSamples == TRUE, "Got fixed size %d.\n", mt->bFixedSizeSamples);
    ok(!mt->bTemporalCompression, "Got temporal compression %d.\n", mt->bTemporalCompression);
    ok(!mt->pUnk, "Got pUnk %p.\n", mt->pUnk);

    ok(EqualRect(&video_info->rcSource, &rect), "Got source rect %s.\n", wine_dbgstr_rect(&rect));
    ok(EqualRect(&video_info->rcTarget, &rect), "Got target rect %s.\n", wine_dbgstr_rect(&rect));
    ok(!video_info->dwBitRate, "Got bit rate %u.\n", video_info->dwBitRate);
    ok(!video_info->dwBitErrorRate, "Got bit error rate %u.\n", video_info->dwBitErrorRate);
    ok(video_info->bmiHeader.biSize == sizeof(video_info->bmiHeader),
            "Got size %u.\n", video_info->bmiHeader.biSize);
    ok(video_info->bmiHeader.biWidth == 64, "Got width %d.\n", video_info->bmiHeader.biWidth);
    ok(video_info->bmiHeader.biHeight == 48, "Got height %d.\n", video_info->bmiHeader.biHeight);
    ok(video_info->bmiHeader.biPlanes == 1, "Got planes %d.\n", video_info->bmiHeader.biPlanes);
}

static void check_audio_type(const WM_MEDIA_TYPE *mt)
{
    const WAVEFORMATEX *wave_format = (const WAVEFORMATEX *)mt->pbFormat;

    ok(IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_PCM), "Got subtype %s.\n", debugstr_guid(&mt->subtype));
    ok(IsEqualGUID(&mt->formattype, &FORMAT_WaveFormatEx), "Got format %s.\n", debugstr_guid(&mt->formattype));
    ok(mt->bFixedSizeSamples == TRUE, "Got fixed size %d.\n", mt->bFixedSizeSamples);
    ok(!mt->bTemporalCompression, "Got temporal compression %d.\n", mt->bTemporalCompression);
    ok(!mt->pUnk, "Got pUnk %p.\n", mt->pUnk);

    ok(wave_format->wFormatTag == WAVE_FORMAT_PCM, "Got tag %#x.\n", wave_format->wFormatTag);
}

static void test_sync_reader_types(void)
{
    char mt_buffer[2000], mt2_buffer[2000];
    const WCHAR *filename = load_resource(L"test.wmv");
    IWMOutputMediaProps *output_props, *output_props2;
    WM_MEDIA_TYPE *mt2 = (WM_MEDIA_TYPE *)mt2_buffer;
    WM_MEDIA_TYPE *mt = (WM_MEDIA_TYPE *)mt_buffer;
    bool got_video = false, got_audio = false;
    DWORD size, ret_size, output_number;
    WORD stream_number, stream_number2;
    struct teststream stream;
    IWMStreamConfig *config;
    ULONG count, ref, i, j;
    IWMSyncReader *reader;
    IWMProfile *profile;
    GUID majortype;
    HANDLE file;
    HRESULT hr;
    BOOL ret;

    file = CreateFileW(filename, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, 0);
    ok(file != INVALID_HANDLE_VALUE, "Failed to open %s, error %u.\n", debugstr_w(file), GetLastError());

    teststream_init(&stream, file);

    hr = WMCreateSyncReader(NULL, 0, &reader);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    IWMSyncReader_QueryInterface(reader, &IID_IWMProfile, (void **)&profile);

    hr = IWMSyncReader_OpenStream(reader, &stream.IStream_iface);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(stream.refcount > 1, "Got refcount %d.\n", stream.refcount);

    for (i = 0; i < 2; ++i)
    {
        winetest_push_context("Stream %u", i);

        hr = IWMProfile_GetStream(profile, i, &config);
        ok(hr == S_OK, "Got hr %#x.\n", hr);

        stream_number = 0xdead;
        hr = IWMStreamConfig_GetStreamNumber(config, &stream_number);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        ok(stream_number == i + 1, "Got stream number %u.\n", stream_number);

        hr = IWMStreamConfig_GetStreamType(config, &majortype);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        if (!i)
            ok(IsEqualGUID(&majortype, &MEDIATYPE_Video), "Got major type %s.\n", debugstr_guid(&majortype));
        else
            ok(IsEqualGUID(&majortype, &MEDIATYPE_Audio), "Got major type %s.\n", debugstr_guid(&majortype));

        ref = IWMStreamConfig_Release(config);
        ok(!ref, "Got outstanding refcount %d.\n", ref);

        output_number = 0xdeadbeef;
        hr = IWMSyncReader_GetOutputNumberForStream(reader, stream_number, &output_number);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        todo_wine ok(output_number == 1 - i, "Got output number %u.\n", output_number);

        stream_number2 = 0xdead;
        hr = IWMSyncReader_GetStreamNumberForOutput(reader, output_number, &stream_number2);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        ok(stream_number2 == stream_number, "Expected stream number %u, got %u.\n", stream_number, stream_number2);

        hr = IWMSyncReader_GetOutputProps(reader, output_number, &output_props);
        ok(hr == S_OK, "Got hr %#x.\n", hr);

        ret_size = sizeof(mt_buffer);
        hr = IWMOutputMediaProps_GetMediaType(output_props, mt, &ret_size);
        ok(hr == S_OK, "Got hr %#x.\n", hr);

        ref = IWMOutputMediaProps_Release(output_props);
        ok(!ref, "Got outstanding refcount %d.\n", ref);

        if (IsEqualGUID(&majortype, &MEDIATYPE_Audio))
        {
            got_audio = true;
            check_audio_type(mt);
        }
        else
        {
            ok(IsEqualGUID(&majortype, &MEDIATYPE_Video), "Got major type %s.\n", debugstr_guid(&majortype));
            got_video = true;
            check_video_type(mt);
        }

        count = 0;
        hr = IWMSyncReader_GetOutputFormatCount(reader, output_number, &count);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        ok(count > 0, "Got count %u.\n", count);

        for (j = 0; j < count; ++j)
        {
            winetest_push_context("Format %u", j);

            hr = IWMSyncReader_GetOutputFormat(reader, output_number, j, &output_props);
            ok(hr == S_OK, "Got hr %#x.\n", hr);

            hr = IWMSyncReader_GetOutputFormat(reader, output_number, j, &output_props2);
            ok(hr == S_OK, "Got hr %#x.\n", hr);
            ok(output_props2 != output_props, "Expected different objects.\n");
            ref = IWMOutputMediaProps_Release(output_props2);
            ok(!ref, "Got outstanding refcount %d.\n", ref);

            size = 0xdeadbeef;
            hr = IWMOutputMediaProps_GetMediaType(output_props, NULL, &size);
            ok(hr == S_OK, "Got hr %#x.\n", hr);
            ok(size != 0xdeadbeef && size >= sizeof(WM_MEDIA_TYPE), "Got size %u.\n", size);

            ret_size = size - 1;
            hr = IWMOutputMediaProps_GetMediaType(output_props, mt, &ret_size);
            ok(hr == ASF_E_BUFFERTOOSMALL, "Got hr %#x.\n", hr);
            ok(ret_size == size, "Expected size %u, got %u.\n", size, ret_size);

            ret_size = sizeof(mt_buffer);
            memset(mt_buffer, 0xcc, sizeof(mt_buffer));
            hr = IWMOutputMediaProps_GetMediaType(output_props, mt, &ret_size);
            ok(hr == S_OK, "Got hr %#x.\n", hr);
            ok(ret_size == size, "Expected size %u, got %u.\n", size, ret_size);
            ok(size == sizeof(WM_MEDIA_TYPE) + mt->cbFormat, "Expected size %u, got %u.\n",
                    sizeof(WM_MEDIA_TYPE) + mt->cbFormat, size);

            ok(IsEqualGUID(&mt->majortype, &majortype), "Got major type %s.\n", debugstr_guid(&mt->majortype));

            if (IsEqualGUID(&mt->majortype, &MEDIATYPE_Audio))
                check_audio_type(mt);
            else
                check_video_type(mt);

            hr = IWMSyncReader_SetOutputProps(reader, output_number, output_props);
            ok(hr == S_OK, "Got hr %#x.\n", hr);
            hr = IWMSyncReader_SetOutputProps(reader, 1 - output_number, output_props);
            if (!i)
                todo_wine ok(hr == ASF_E_BADMEDIATYPE, "Got hr %#x.\n", hr);
            else
                ok(hr == NS_E_INCOMPATIBLE_FORMAT
                        || hr == NS_E_INVALID_OUTPUT_FORMAT /* win10 */, "Got hr %#x.\n", hr);
            hr = IWMSyncReader_SetOutputProps(reader, 2, output_props);
            ok(hr == E_INVALIDARG, "Got hr %#x.\n", hr);

            hr = IWMSyncReader_GetOutputProps(reader, output_number, &output_props2);
            ok(hr == S_OK, "Got hr %#x.\n", hr);
            ok(output_props2 != output_props, "Expected different objects.\n");

            ret_size = sizeof(mt2_buffer);
            hr = IWMOutputMediaProps_GetMediaType(output_props2, mt2, &ret_size);
            ok(hr == S_OK, "Got hr %#x.\n", hr);
            ok(compare_media_types(mt, mt2), "Media types didn't match.\n");

            ref = IWMOutputMediaProps_Release(output_props2);
            ok(!ref, "Got outstanding refcount %d.\n", ref);
            ref = IWMOutputMediaProps_Release(output_props);
            ok(!ref, "Got outstanding refcount %d.\n", ref);

            winetest_pop_context();
        }

        hr = IWMSyncReader_GetOutputFormat(reader, output_number, count, &output_props);
        ok(hr == NS_E_INVALID_OUTPUT_FORMAT, "Got hr %#x.\n", hr);

        hr = IWMSyncReader_GetOutputProps(reader, output_number, &output_props);
        ok(hr == S_OK, "Got hr %#x.\n", hr);

        hr = IWMSyncReader_GetOutputProps(reader, output_number, &output_props2);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        ok(output_props2 != output_props, "Expected different objects.\n");

        ref = IWMOutputMediaProps_Release(output_props2);
        ok(!ref, "Got outstanding refcount %d.\n", ref);
        ref = IWMOutputMediaProps_Release(output_props);
        ok(!ref, "Got outstanding refcount %d.\n", ref);

        winetest_pop_context();
    }

    ok(got_audio, "No audio stream was enumerated.\n");
    ok(got_video, "No video stream was enumerated.\n");

    count = 0xdeadbeef;
    hr = IWMSyncReader_GetOutputFormatCount(reader, 2, &count);
    ok(hr == E_INVALIDARG, "Got hr %#x.\n", hr);
    ok(count == 0xdeadbeef, "Got count %#x.\n", count);

    output_props = (void *)0xdeadbeef;
    hr = IWMSyncReader_GetOutputProps(reader, 2, &output_props);
    ok(hr == E_INVALIDARG, "Got hr %#x.\n", hr);
    ok(output_props == (void *)0xdeadbeef, "Got output props %p.\n", output_props);

    output_props = (void *)0xdeadbeef;
    hr = IWMSyncReader_GetOutputFormat(reader, 2, 0, &output_props);
    ok(hr == E_INVALIDARG, "Got hr %#x.\n", hr);
    ok(output_props == (void *)0xdeadbeef, "Got output props %p.\n", output_props);

    IWMProfile_Release(profile);
    ref = IWMSyncReader_Release(reader);
    ok(!ref, "Got outstanding refcount %d.\n", ref);

    ok(stream.refcount == 1, "Got outstanding refcount %d.\n", stream.refcount);
    CloseHandle(stream.file);
    ret = DeleteFileW(filename);
    ok(ret, "Failed to delete %s, error %u.\n", debugstr_w(filename), GetLastError());
}

static void test_sync_reader_file(void)
{
    const WCHAR *filename = load_resource(L"test.wmv");
    IWMSyncReader *reader;
    IWMProfile *profile;
    DWORD count;
    HRESULT hr;
    ULONG ref;
    BOOL ret;

    hr = WMCreateSyncReader(NULL, 0, &reader);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    IWMSyncReader_QueryInterface(reader, &IID_IWMProfile, (void **)&profile);

    hr = IWMSyncReader_Open(reader, filename);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    count = 0xdeadbeef;
    hr = IWMSyncReader_GetOutputCount(reader, &count);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(count == 2, "Got count %u.\n", count);

    hr = IWMSyncReader_Close(reader);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IWMSyncReader_Close(reader);
    ok(hr == NS_E_INVALID_REQUEST, "Got hr %#x.\n", hr);

    hr = IWMSyncReader_Open(reader, filename);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    IWMProfile_Release(profile);
    ref = IWMSyncReader_Release(reader);
    ok(!ref, "Got outstanding refcount %d.\n", ref);

    ret = DeleteFileW(filename);
    ok(ret, "Failed to delete %s, error %u.\n", debugstr_w(filename), GetLastError());
}

struct callback
{
    IWMReaderCallback IWMReaderCallback_iface;
    LONG refcount;
    HANDLE got_opened, got_stopped, eof_event;
    unsigned int got_closed, got_started, got_sample, got_end_of_streaming, got_eof;
};

static struct callback *impl_from_IWMReaderCallback(IWMReaderCallback *iface)
{
    return CONTAINING_RECORD(iface, struct callback, IWMReaderCallback_iface);
}

static HRESULT WINAPI callback_QueryInterface(IWMReaderCallback *iface, REFIID iid, void **out)
{
    if (winetest_debug > 1)
        trace("%04x: IWMReaderCallback::QueryInterface(%s)\n", GetCurrentThreadId(), debugstr_guid(iid));

    if (!IsEqualGUID(iid, &IID_IWMReaderCallbackAdvanced) && !IsEqualGUID(iid, &IID_IWMCredentialCallback))
        ok(0, "Unexpected IID %s.\n", debugstr_guid(iid));

    return E_NOINTERFACE;
}

static ULONG WINAPI callback_AddRef(IWMReaderCallback *iface)
{
    struct callback *callback = impl_from_IWMReaderCallback(iface);

    return InterlockedIncrement(&callback->refcount);
}

static ULONG WINAPI callback_Release(IWMReaderCallback *iface)
{
    struct callback *callback = impl_from_IWMReaderCallback(iface);

    return InterlockedDecrement(&callback->refcount);
}

static HRESULT WINAPI callback_OnStatus(IWMReaderCallback *iface, WMT_STATUS status,
        HRESULT hr, WMT_ATTR_DATATYPE type, BYTE *value, void *context)
{
    struct callback *callback = impl_from_IWMReaderCallback(iface);

    if (winetest_debug > 1)
        trace("%u: %04x: IWMReaderCallback::OnStatus(status %u, hr %#x, type %#x, value %p)\n",
                GetTickCount(), GetCurrentThreadId(), status, hr, type, value);

    switch (status)
    {
        case WMT_OPENED:
            ok(type == WMT_TYPE_DWORD, "Got type %#x.\n", type);
            ok(!*(DWORD *)value, "Got value %#x.\n", *(DWORD *)value);
            ok(context == (void *)0xdeadbeef, "Got unexpected context %p.\n", context);
            SetEvent(callback->got_opened);
            break;

        case WMT_STARTED:
            ok(type == WMT_TYPE_DWORD, "Got type %#x.\n", type);
            ok(!*(DWORD *)value, "Got value %#x.\n", *(DWORD *)value);
            ok(context == (void *)0xfacade, "Got unexpected context %p.\n", context);
            ++callback->got_started;
            break;

        case WMT_STOPPED:
            ok(type == WMT_TYPE_DWORD, "Got type %#x.\n", type);
            ok(!*(DWORD *)value, "Got value %#x.\n", *(DWORD *)value);
            ok(context == (void *)0xfacade, "Got unexpected context %p.\n", context);
            SetEvent(callback->got_stopped);
            break;

        case WMT_CLOSED:
            ok(type == WMT_TYPE_DWORD, "Got type %#x.\n", type);
            ok(!*(DWORD *)value, "Got value %#x.\n", *(DWORD *)value);
            todo_wine ok(context == (void *)0xfacade, "Got unexpected context %p.\n", context);
            ++callback->got_closed;
            break;

        case WMT_END_OF_STREAMING:
            ok(type == WMT_TYPE_DWORD, "Got type %#x.\n", type);
            ok(!*(DWORD *)value, "Got value %#x.\n", *(DWORD *)value);
            ok(context == (void *)0xfacade, "Got unexpected context %p.\n", context);
            ok(!callback->got_eof, "Got %u WMT_EOF callbacks.\n", callback->got_eof);
            ++callback->got_end_of_streaming;
            break;

        case WMT_EOF:
            ok(type == WMT_TYPE_DWORD, "Got type %#x.\n", type);
            ok(!*(DWORD *)value, "Got value %#x.\n", *(DWORD *)value);
            ok(context == (void *)0xfacade, "Got unexpected context %p.\n", context);
            ok(callback->got_sample > 0, "Got no samples.\n");
            ok(callback->got_end_of_streaming == 1, "Got %u WMT_END_OF_STREAMING callbacks.\n",
                    callback->got_end_of_streaming);
            ++callback->got_eof;
            SetEvent(callback->eof_event);
            break;

        /* Not sent when not using IWMReaderAdvanced::DeliverTime(). */
        case WMT_END_OF_SEGMENT:
            ok(type == WMT_TYPE_QWORD, "Got type %#x.\n", type);
            ok(*(QWORD *)value == 3000, "Got value %#x.\n", *(DWORD *)value);
            ok(context == (void *)0xfacade, "Got unexpected context %p.\n", context);
            ok(callback->got_sample > 0, "Got no samples.\n");
            ok(callback->got_eof == 1, "Got %u WMT_EOF callbacks.\n", callback->got_eof);
            break;

        default:
            ok(0, "Unexpected status %#x.\n", status);
    }

    ok(hr == S_OK, "Got hr %#x.\n", hr);
    return S_OK;
}

static HRESULT WINAPI callback_OnSample(IWMReaderCallback *iface, DWORD output,
        QWORD time, QWORD duration, DWORD flags, INSSBuffer *sample, void *context)
{
    struct callback *callback = impl_from_IWMReaderCallback(iface);
    HRESULT hr;
    DWORD size;
    BYTE *data;

    if (winetest_debug > 1)
        trace("%u: %04x: IWMReaderCallback::OnSample(output %u, time %I64u, duration %I64u, flags %#x)\n",
                GetTickCount(), GetCurrentThreadId(), output, time, duration, flags);

    ok(context == (void *)0xfacade, "Got unexpected context %p.\n", context);

    hr = INSSBuffer_GetBufferAndLength(sample, &data, &size);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    ok(callback->got_started > 0, "Got %u WMT_STARTED callbacks.\n", callback->got_started);
    ok(!callback->got_eof, "Got %u WMT_EOF callbacks.\n", callback->got_eof);
    ++callback->got_sample;

    return S_OK;
}

static const IWMReaderCallbackVtbl callback_vtbl =
{
    callback_QueryInterface,
    callback_AddRef,
    callback_Release,
    callback_OnStatus,
    callback_OnSample,
};

static void callback_init(struct callback *callback)
{
    memset(callback, 0, sizeof(*callback));
    callback->IWMReaderCallback_iface.lpVtbl = &callback_vtbl;
    callback->refcount = 1;
    callback->got_opened = CreateEventW(NULL, FALSE, FALSE, NULL);
    callback->got_stopped = CreateEventW(NULL, FALSE, FALSE, NULL);
    callback->eof_event = CreateEventW(NULL, FALSE, FALSE, NULL);
}

static void callback_cleanup(struct callback *callback)
{
    CloseHandle(callback->got_opened);
    CloseHandle(callback->got_stopped);
    CloseHandle(callback->eof_event);
}

static void test_async_reader_streaming(void)
{
    const WCHAR *filename = load_resource(L"test.wmv");
    IWMReaderAdvanced2 *advanced;
    struct teststream stream;
    struct callback callback;
    IWMStreamConfig *config;
    WORD stream_numbers[2];
    IWMProfile *profile;
    ULONG i, count, ref;
    IWMReader *reader;
    HANDLE file;
    HRESULT hr;
    BOOL ret;

    file = CreateFileW(filename, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, 0);
    ok(file != INVALID_HANDLE_VALUE, "Failed to open %s, error %u.\n", debugstr_w(file), GetLastError());

    teststream_init(&stream, file);
    callback_init(&callback);

    hr = WMCreateReader(NULL, 0, &reader);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    IWMReader_QueryInterface(reader, &IID_IWMProfile, (void **)&profile);
    IWMReader_QueryInterface(reader, &IID_IWMReaderAdvanced2, (void **)&advanced);

    hr = IWMReaderAdvanced2_OpenStream(advanced, &stream.IStream_iface, &callback.IWMReaderCallback_iface, (void **)0xdeadbeef);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(stream.refcount > 1, "Got refcount %d.\n", stream.refcount);
    ok(callback.refcount > 1, "Got refcount %d.\n", callback.refcount);
    ret = WaitForSingleObject(callback.got_opened, 1000);
    ok(!ret, "Wait timed out.\n");

    count = 0xdeadbeef;
    hr = IWMReader_GetOutputCount(reader, &count);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(count == 2, "Got count %u.\n", count);

    for (i = 0; i < 2; ++i)
    {
        hr = IWMProfile_GetStream(profile, i, &config);
        ok(hr == S_OK, "Got hr %#x.\n", hr);

        stream_numbers[i] = 0xdead;
        hr = IWMStreamConfig_GetStreamNumber(config, &stream_numbers[i]);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        ok(stream_numbers[i] == i + 1, "Got stream number %u.\n", stream_numbers[i]);

        ref = IWMStreamConfig_Release(config);
        ok(!ref, "Got outstanding refcount %d.\n", ref);
    }

    hr = IWMReader_Start(reader, 0, 0, 1.0f, (void *)0xfacade);
    todo_wine ok(hr == S_OK, "Got hr %#x.\n", hr);

    if (hr == S_OK)
    {
        /* By default the reader will time itself, and attempt to deliver samples
         * according to their presentation time. Call DeliverTime with the file
         * duration in order to request all samples as fast as possible. */
        hr = IWMReaderAdvanced2_DeliverTime(advanced, 3000 * 10000);
        ok(hr == E_UNEXPECTED, "Got hr %#x.\n", hr);
        hr = IWMReaderAdvanced2_SetUserProvidedClock(advanced, TRUE);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        hr = IWMReaderAdvanced2_DeliverTime(advanced, 3000 * 10000);
        ok(hr == S_OK, "Got hr %#x.\n", hr);

        ret = WaitForSingleObject(callback.eof_event, 1000);
        ok(!ret, "Wait timed out.\n");
        ok(callback.got_eof == 1, "Got %u WMT_EOF callbacks.\n", callback.got_eof);

        hr = IWMReader_Stop(reader);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        ret = WaitForSingleObject(callback.got_stopped, 1000);
        ok(!ret, "Wait timed out.\n");

        hr = IWMReader_Stop(reader);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        ret = WaitForSingleObject(callback.got_stopped, 1000);
        ok(!ret, "Wait timed out.\n");
    }

    test_reader_attributes(profile);

    hr = IWMReader_Close(reader);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(callback.got_closed == 1, "Got %u WMT_CLOSED callbacks.\n", callback.got_closed);
    ok(callback.refcount == 1, "Got outstanding refcount %d.\n", callback.refcount);
    callback_cleanup(&callback);

    ok(stream.refcount == 1, "Got outstanding refcount %d.\n", stream.refcount);
    CloseHandle(stream.file);
    ret = DeleteFileW(filename);
    ok(ret, "Failed to delete %s, error %u.\n", debugstr_w(filename), GetLastError());

    hr = IWMReader_Close(reader);
    ok(hr == NS_E_INVALID_REQUEST, "Got hr %#x.\n", hr);

    IWMReaderAdvanced2_Release(advanced);
    IWMProfile_Release(profile);
    ref = IWMReader_Release(reader);
    ok(!ref, "Got outstanding refcount %d.\n", ref);
}

static void test_async_reader_types(void)
{
    char mt_buffer[2000], mt2_buffer[2000];
    const WCHAR *filename = load_resource(L"test.wmv");
    IWMOutputMediaProps *output_props, *output_props2;
    WM_MEDIA_TYPE *mt2 = (WM_MEDIA_TYPE *)mt2_buffer;
    WM_MEDIA_TYPE *mt = (WM_MEDIA_TYPE *)mt_buffer;
    bool got_video = false, got_audio = false;
    DWORD size, ret_size, output_number;
    IWMReaderAdvanced2 *advanced;
    struct teststream stream;
    struct callback callback;
    IWMStreamConfig *config;
    ULONG count, ref, i, j;
    IWMProfile *profile;
    IWMReader *reader;
    GUID majortype;
    HANDLE file;
    HRESULT hr;
    BOOL ret;

    file = CreateFileW(filename, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, 0);
    ok(file != INVALID_HANDLE_VALUE, "Failed to open %s, error %u.\n", debugstr_w(file), GetLastError());

    teststream_init(&stream, file);
    callback_init(&callback);

    hr = WMCreateReader(NULL, 0, &reader);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    IWMReader_QueryInterface(reader, &IID_IWMProfile, (void **)&profile);
    IWMReader_QueryInterface(reader, &IID_IWMReaderAdvanced2, (void **)&advanced);

    hr = IWMReaderAdvanced2_OpenStream(advanced, &stream.IStream_iface, &callback.IWMReaderCallback_iface, (void **)0xdeadbeef);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(stream.refcount > 1, "Got refcount %d.\n", stream.refcount);
    ok(callback.refcount > 1, "Got refcount %d.\n", callback.refcount);
    ret = WaitForSingleObject(callback.got_opened, 1000);
    ok(!ret, "Wait timed out.\n");

    for (i = 0; i < 2; ++i)
    {
        winetest_push_context("Stream %u", i);

        hr = IWMProfile_GetStream(profile, i, &config);
        ok(hr == S_OK, "Got hr %#x.\n", hr);

        hr = IWMStreamConfig_GetStreamType(config, &majortype);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        if (!i)
            ok(IsEqualGUID(&majortype, &MEDIATYPE_Video), "Got major type %s.\n", debugstr_guid(&majortype));
        else
            ok(IsEqualGUID(&majortype, &MEDIATYPE_Audio), "Got major type %s.\n", debugstr_guid(&majortype));

        ref = IWMStreamConfig_Release(config);
        ok(!ref, "Got outstanding refcount %d.\n", ref);

        winetest_pop_context();
    }

    for (i = 0; i < 2; ++i)
    {
        winetest_push_context("Output %u", i);
        output_number = i;

        hr = IWMReader_GetOutputProps(reader, output_number, &output_props);
        ok(hr == S_OK, "Got hr %#x.\n", hr);

        ret_size = sizeof(mt_buffer);
        hr = IWMOutputMediaProps_GetMediaType(output_props, mt, &ret_size);
        ok(hr == S_OK, "Got hr %#x.\n", hr);

        ref = IWMOutputMediaProps_Release(output_props);
        ok(!ref, "Got outstanding refcount %d.\n", ref);

        majortype = mt->majortype;
        if (IsEqualGUID(&majortype, &MEDIATYPE_Audio))
        {
            got_audio = true;
            check_audio_type(mt);

            /* R.U.S.E. enumerates all audio formats, picks the first one it
             * likes, and then sets the wrong stream to that format.
             * Accordingly we need the first audio format to be the default
             * format, and we need it to be a format that the game is happy
             * with. In particular it has to be PCM. */

            hr = IWMReader_GetOutputFormat(reader, output_number, 0, &output_props);
            todo_wine ok(hr == S_OK, "Got hr %#x.\n", hr);

            if (hr == S_OK)
            {
                ret_size = sizeof(mt2_buffer);
                hr = IWMOutputMediaProps_GetMediaType(output_props, mt2, &ret_size);
                ok(hr == S_OK, "Got hr %#x.\n", hr);

                ref = IWMOutputMediaProps_Release(output_props);
                ok(!ref, "Got outstanding refcount %d.\n", ref);

                /* The sample size might differ. */
                mt2->lSampleSize = mt->lSampleSize;
                ok(compare_media_types(mt, mt2), "Media types didn't match.\n");
            }
        }
        else
        {
            ok(IsEqualGUID(&majortype, &MEDIATYPE_Video), "Got major type %s.\n", debugstr_guid(&majortype));
            got_video = true;
            check_video_type(mt);
        }

        count = 0;
        hr = IWMReader_GetOutputFormatCount(reader, output_number, &count);
        todo_wine ok(hr == S_OK, "Got hr %#x.\n", hr);
        todo_wine ok(count > 0, "Got count %u.\n", count);

        for (j = 0; j < count; ++j)
        {
            winetest_push_context("Format %u", j);

            hr = IWMReader_GetOutputFormat(reader, output_number, j, &output_props);
            ok(hr == S_OK, "Got hr %#x.\n", hr);

            hr = IWMReader_GetOutputFormat(reader, output_number, j, &output_props2);
            ok(hr == S_OK, "Got hr %#x.\n", hr);
            ok(output_props2 != output_props, "Expected different objects.\n");
            ref = IWMOutputMediaProps_Release(output_props2);
            ok(!ref, "Got outstanding refcount %d.\n", ref);

            size = 0xdeadbeef;
            hr = IWMOutputMediaProps_GetMediaType(output_props, NULL, &size);
            ok(hr == S_OK, "Got hr %#x.\n", hr);
            ok(size != 0xdeadbeef && size >= sizeof(WM_MEDIA_TYPE), "Got size %u.\n", size);

            ret_size = size - 1;
            hr = IWMOutputMediaProps_GetMediaType(output_props, mt, &ret_size);
            ok(hr == ASF_E_BUFFERTOOSMALL, "Got hr %#x.\n", hr);
            ok(ret_size == size, "Expected size %u, got %u.\n", size, ret_size);

            ret_size = sizeof(mt_buffer);
            memset(mt_buffer, 0xcc, sizeof(mt_buffer));
            hr = IWMOutputMediaProps_GetMediaType(output_props, mt, &ret_size);
            ok(hr == S_OK, "Got hr %#x.\n", hr);
            ok(ret_size == size, "Expected size %u, got %u.\n", size, ret_size);
            ok(size == sizeof(WM_MEDIA_TYPE) + mt->cbFormat, "Expected size %u, got %u.\n",
                    sizeof(WM_MEDIA_TYPE) + mt->cbFormat, size);

            ok(IsEqualGUID(&mt->majortype, &majortype), "Got major type %s.\n", debugstr_guid(&mt->majortype));

            if (IsEqualGUID(&mt->majortype, &MEDIATYPE_Audio))
                check_audio_type(mt);
            else
                check_video_type(mt);

            hr = IWMReader_SetOutputProps(reader, output_number, output_props);
            todo_wine ok(hr == S_OK, "Got hr %#x.\n", hr);
            if (hr != S_OK)
            {
                ref = IWMOutputMediaProps_Release(output_props);
                ok(!ref, "Got outstanding refcount %d.\n", ref);
                winetest_pop_context();
                continue;
            }
            hr = IWMReader_SetOutputProps(reader, 1 - output_number, output_props);
            if (!i)
                ok(hr == NS_E_INCOMPATIBLE_FORMAT /* win < 8, win10 1507-1809 */
                        || hr == ASF_E_BADMEDIATYPE /* win8, win10 1909+ */, "Got hr %#x.\n", hr);
            else
                todo_wine ok(hr == NS_E_INVALID_REQUEST, "Got hr %#x.\n", hr);
            hr = IWMReader_SetOutputProps(reader, 2, output_props);
            todo_wine ok(hr == E_INVALIDARG, "Got hr %#x.\n", hr);

            hr = IWMReader_GetOutputProps(reader, output_number, &output_props2);
            ok(hr == S_OK, "Got hr %#x.\n", hr);
            ok(output_props2 != output_props, "Expected different objects.\n");

            ret_size = sizeof(mt2_buffer);
            hr = IWMOutputMediaProps_GetMediaType(output_props2, mt2, &ret_size);
            ok(hr == S_OK, "Got hr %#x.\n", hr);
            ok(compare_media_types(mt, mt2), "Media types didn't match.\n");

            ref = IWMOutputMediaProps_Release(output_props2);
            ok(!ref, "Got outstanding refcount %d.\n", ref);
            ref = IWMOutputMediaProps_Release(output_props);
            ok(!ref, "Got outstanding refcount %d.\n", ref);

            winetest_pop_context();
        }

        hr = IWMReader_GetOutputFormat(reader, output_number, count, &output_props);
        todo_wine ok(hr == NS_E_INVALID_OUTPUT_FORMAT, "Got hr %#x.\n", hr);

        hr = IWMReader_GetOutputProps(reader, output_number, &output_props);
        ok(hr == S_OK, "Got hr %#x.\n", hr);

        hr = IWMReader_GetOutputProps(reader, output_number, &output_props2);
        ok(hr == S_OK, "Got hr %#x.\n", hr);
        ok(output_props2 != output_props, "Expected different objects.\n");

        ref = IWMOutputMediaProps_Release(output_props2);
        ok(!ref, "Got outstanding refcount %d.\n", ref);
        ref = IWMOutputMediaProps_Release(output_props);
        ok(!ref, "Got outstanding refcount %d.\n", ref);

        winetest_pop_context();
    }

    ok(got_audio, "No audio stream was enumerated.\n");
    ok(got_video, "No video stream was enumerated.\n");

    count = 0xdeadbeef;
    hr = IWMReader_GetOutputFormatCount(reader, 2, &count);
    todo_wine ok(hr == E_INVALIDARG, "Got hr %#x.\n", hr);
    ok(count == 0xdeadbeef, "Got count %#x.\n", count);

    output_props = (void *)0xdeadbeef;
    hr = IWMReader_GetOutputProps(reader, 2, &output_props);
    ok(hr == E_INVALIDARG, "Got hr %#x.\n", hr);
    ok(output_props == (void *)0xdeadbeef, "Got output props %p.\n", output_props);

    output_props = (void *)0xdeadbeef;
    hr = IWMReader_GetOutputFormat(reader, 2, 0, &output_props);
    todo_wine ok(hr == E_INVALIDARG, "Got hr %#x.\n", hr);
    ok(output_props == (void *)0xdeadbeef, "Got output props %p.\n", output_props);

    IWMReaderAdvanced2_Release(advanced);
    IWMProfile_Release(profile);
    ref = IWMReader_Release(reader);
    ok(!ref, "Got outstanding refcount %d.\n", ref);

    ok(stream.refcount == 1, "Got outstanding refcount %d.\n", stream.refcount);
    CloseHandle(stream.file);
    ret = DeleteFileW(filename);
    ok(ret, "Failed to delete %s, error %u.\n", debugstr_w(filename), GetLastError());
}

START_TEST(wmvcore)
{
    HRESULT hr;

    hr = CoInitialize(0);
    ok(hr == S_OK, "failed to init com\n");
    if(hr != S_OK)
        return;

    test_wmreader_interfaces();
    test_wmsyncreader_interfaces();
    test_wmwriter_interfaces();
    test_profile_manager_interfaces();
    test_WMCreateWriterPriv();
    test_urlextension();
    test_iscontentprotected();
    test_sync_reader_streaming();
    test_sync_reader_types();
    test_sync_reader_file();
    test_async_reader_streaming();
    test_async_reader_types();

    CoUninitialize();
}

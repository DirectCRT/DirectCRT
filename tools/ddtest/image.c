/*
 * Image file loading and PNG saving through WIC (Windows Imaging Component),
 * which handles PNG, BMP, JPEG, GIF and TIFF.
 */

#define COBJMACROS
#include "ddtest.h"
#include <stdio.h>
#include <stdlib.h>
#include <initguid.h>
#include <wincodec.h>

static void set_err(char* err, int err_len, const char* what, HRESULT hr)
{
    if (err && err_len > 0)
    {
        _snprintf(err, err_len - 1, "%s (0x%08lX)", what, (unsigned long)hr);
        err[err_len - 1] = '\0';
    }
}

static IWICImagingFactory* create_factory(char* err, int err_len)
{
    IWICImagingFactory* f = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory1, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory, (void**)&f);
    if (FAILED(hr))
    {
        set_err(err, err_len, "Couldn't start the Windows Imaging Component", hr);
        return NULL;
    }
    return f;
}

/* Decodes the first frame of an image into 0x00RRGGBB pixels */
static BOOL decode(IWICBitmapDecoder* dec, uint32_t** px, int* w, int* h, char* err, int err_len)
{
    IWICBitmapFrameDecode* frame = NULL;
    IWICBitmapSource* conv = NULL;
    uint32_t* buf = NULL;
    UINT iw = 0, ih = 0;
    HRESULT hr;

    hr = IWICBitmapDecoder_GetFrame(dec, 0, &frame);
    if (FAILED(hr)) { set_err(err, err_len, "Couldn't read the image", hr); goto done; }

    hr = WICConvertBitmapSource(&GUID_WICPixelFormat32bppBGR, (IWICBitmapSource*)frame, &conv);
    if (FAILED(hr)) { set_err(err, err_len, "Couldn't convert the image", hr); goto done; }

    hr = IWICBitmapSource_GetSize(conv, &iw, &ih);
    if (FAILED(hr) || iw == 0 || ih == 0 || iw > 16384 || ih > 16384)
    {
        set_err(err, err_len, "Unsupported image size", hr);
        hr = E_FAIL;
        goto done;
    }

    buf = malloc((size_t)iw * ih * 4);
    if (!buf) { set_err(err, err_len, "Out of memory", E_OUTOFMEMORY); hr = E_OUTOFMEMORY; goto done; }

    /* 32bppBGR in memory is 0x00RRGGBB per pixel */
    hr = IWICBitmapSource_CopyPixels(conv, NULL, iw * 4, iw * ih * 4, (BYTE*)buf);
    if (FAILED(hr)) { set_err(err, err_len, "Couldn't decode the image", hr); goto done; }

    *px = buf;
    *w = (int)iw;
    *h = (int)ih;
    buf = NULL;

done:
    free(buf);
    if (conv) IWICBitmapSource_Release(conv);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    return SUCCEEDED(hr);
}

BOOL image_load(const char* path, uint32_t** px, int* w, int* h, char* err, int err_len)
{
    WCHAR wpath[MAX_PATH];
    IWICBitmapDecoder* dec = NULL;
    BOOL ok = FALSE;

    *px = NULL;
    *w = *h = 0;

    if (!MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH))
    {
        set_err(err, err_len, "Invalid file name", E_INVALIDARG);
        return FALSE;
    }

    IWICImagingFactory* f = create_factory(err, err_len);
    if (!f)
        return FALSE;

    HRESULT hr = IWICImagingFactory_CreateDecoderFromFilename(f, wpath, NULL, GENERIC_READ,
                                                              WICDecodeMetadataCacheOnDemand, &dec);
    if (FAILED(hr))
        set_err(err, err_len, "Couldn't open the image", hr);
    else
        ok = decode(dec, px, w, h, err, err_len);

    if (dec) IWICBitmapDecoder_Release(dec);
    IWICImagingFactory_Release(f);
    return ok;
}

BOOL image_load_memory(const void* data, size_t size, uint32_t** px, int* w, int* h, char* err, int err_len)
{
    IWICStream* stream = NULL;
    IWICBitmapDecoder* dec = NULL;
    BOOL ok = FALSE;
    HRESULT hr;

    *px = NULL;
    *w = *h = 0;

    IWICImagingFactory* f = create_factory(err, err_len);
    if (!f)
        return FALSE;

    hr = IWICImagingFactory_CreateStream(f, &stream);
    if (SUCCEEDED(hr))
        hr = IWICStream_InitializeFromMemory(stream, (BYTE*)data, (DWORD)size);
    if (SUCCEEDED(hr))
        hr = IWICImagingFactory_CreateDecoderFromStream(f, (IStream*)stream, NULL, WICDecodeMetadataCacheOnDemand, &dec);

    if (FAILED(hr))
        set_err(err, err_len, "Couldn't open the image", hr);
    else
        ok = decode(dec, px, w, h, err, err_len);

    if (dec) IWICBitmapDecoder_Release(dec);
    if (stream) IWICStream_Release(stream);
    IWICImagingFactory_Release(f);
    return ok;
}

BOOL image_save_png(const char* path, const uint32_t* px, int w, int h, char* err, int err_len)
{
    WCHAR wpath[MAX_PATH];
    IWICStream* stream = NULL;
    IWICBitmapEncoder* enc = NULL;
    IWICBitmapFrameEncode* frame = NULL;
    IPropertyBag2* props = NULL;
    uint32_t* bgra = NULL;
    HRESULT hr;

    if (!MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH))
    {
        set_err(err, err_len, "Invalid file name", E_INVALIDARG);
        return FALSE;
    }

    IWICImagingFactory* f = create_factory(err, err_len);
    if (!f)
        return FALSE;

    bgra = malloc((size_t)w * h * 4);
    if (!bgra) { set_err(err, err_len, "Out of memory", E_OUTOFMEMORY); hr = E_OUTOFMEMORY; goto done; }

    for (int i = 0; i < w * h; i++)
        bgra[i] = px[i] | 0xFF000000u;

    hr = IWICImagingFactory_CreateStream(f, &stream);
    if (SUCCEEDED(hr)) hr = IWICStream_InitializeFromFilename(stream, wpath, GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateEncoder(f, &GUID_ContainerFormatPng, NULL, &enc);
    if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_Initialize(enc, (IStream*)stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_CreateNewFrame(enc, &frame, &props);
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_Initialize(frame, props);
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_SetSize(frame, (UINT)w, (UINT)h);

    if (SUCCEEDED(hr))
    {
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        hr = IWICBitmapFrameEncode_SetPixelFormat(frame, &fmt);
        if (SUCCEEDED(hr) && !IsEqualGUID(&fmt, &GUID_WICPixelFormat32bppBGRA))
            hr = E_FAIL;
    }

    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_WritePixels(frame, (UINT)h, (UINT)w * 4, (UINT)(w * h * 4), (BYTE*)bgra);
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_Commit(frame);
    if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_Commit(enc);

    if (FAILED(hr))
        set_err(err, err_len, "Couldn't write the PNG file", hr);

done:
    free(bgra);
    if (props) IPropertyBag2_Release(props);
    if (frame) IWICBitmapFrameEncode_Release(frame);
    if (enc) IWICBitmapEncoder_Release(enc);
    if (stream) IWICStream_Release(stream);
    IWICImagingFactory_Release(f);
    return SUCCEEDED(hr);
}

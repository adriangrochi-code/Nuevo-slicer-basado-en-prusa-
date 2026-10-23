/*

    OpenPnp-Capture: a video capture subsystem.

    Linux platform code, MJPEG decoding using libjpeg-turbo

    Tisma Slicer: decoded with the libjpeg API (the libjpeg-turbo that Tisma
    already links) instead of the TurboJPEG API.

    Created by Niels Moseley on 7/6/17.
    Copyright (c) 2017 Niels Moseley.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

*/

#include "mjpeghelper.h"
#include "../common/logging.h"

#include <csetjmp>
#include <cstdio>
#include <jpeglib.h>

namespace
{
    struct ErrorManager
    {
        jpeg_error_mgr pub;
        jmp_buf        jump;
    };

    // The default handler of libjpeg ends the program: return to decompressFrame instead.
    void onError(j_common_ptr info)
    {
        longjmp(reinterpret_cast<ErrorManager*>(info->err)->jump, 1);
    }

    // A lot of cameras produce incorrect but decodable JPEG data ('extraneous bytes before marker'...): no warnings.
    void onMessage(j_common_ptr) {}
}

bool MJPEGHelper::decompressFrame(const uint8_t *inBuffer,
    size_t inBytes, uint8_t *outBuffer,
    uint32_t outBufWidth, uint32_t outBufHeight)
{
    jpeg_decompress_struct info;
    ErrorManager error;
    info.err = jpeg_std_error(&error.pub);
    error.pub.error_exit = onError;
    error.pub.output_message = onMessage;
    if (setjmp(error.jump))
    {
        jpeg_destroy_decompress(&info);
        LOG(LOG_ERR, "MJPG: the frame could not be decoded\n");
        return false;
    }
    jpeg_create_decompress(&info);
    // MJPEG frames of webcams often have no Huffman tables: libjpeg-turbo uses the standard ones then.
    jpeg_mem_src(&info, const_cast<unsigned char*>(inBuffer), static_cast<unsigned long>(inBytes));
    jpeg_read_header(&info, TRUE);
    if ((info.image_width != outBufWidth) || (info.image_height != outBufHeight))
    {
        LOG(LOG_ERR, "MJPG: %d x %d, expected %d x %d\n", info.image_width, info.image_height, outBufWidth, outBufHeight);
        jpeg_destroy_decompress(&info);
        return false;
    }
    LOG(LOG_VERBOSE, "MJPG: %d %d size %d bytes\n", info.image_width, info.image_height, (int)inBytes);
    info.out_color_space = JCS_RGB;
    info.dct_method = JDCT_IFAST;
    jpeg_start_decompress(&info);
    while (info.output_scanline < info.output_height)
    {
        JSAMPROW row = outBuffer + size_t(info.output_scanline) * outBufWidth * 3;
        jpeg_read_scanlines(&info, &row, 1);
    }
    jpeg_finish_decompress(&info);
    jpeg_destroy_decompress(&info);
    return true;
}

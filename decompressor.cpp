#include "decompressor.h"
#include <string.h>

const uint8_t ZSTD_header[4] = { 0x28, 0xB5, 0x2F, 0xFD };

bool decompress(ReadStream* source, std::vector<uint8_t>& result, size_t expected_size) {
    if (!source) return false;
    if (!source->seekable()) return false;
    uint8_t header[4];
    size_t readed = source->read(header, sizeof(header));
    if (!readed) {
        source->close();
        delete source;
        return false;
    }
    if (!source->seek(-((int64_t)readed), SEEK_CUR)) {
        source->close();
        delete source;
        return false;
    }
#if HAVE_ZSTD
    ReadStream* dstream = nullptr;
    if (readed >= 4 && !memcmp(ZSTD_header, header, 4)) {
        dstream = new ZstdDecompressor(source);
    } else {
        dstream = new ZlibDecompressor(source);
    }
#else
    ReadStream* dstream = new ZlibDecompressor(source);
#endif
    if (expected_size > 0) {
        result.resize(expected_size);
        size_t total_readed = 0;
        while (total_readed < expected_size) {
            size_t r = dstream->read(result.data() + total_readed, expected_size - total_readed);
            if (r == 0) break;
            total_readed += r;
        }
        result.resize(total_readed);
        auto re = !dstream->error() && total_readed == expected_size;
        delete dstream;
        return re;
    } else {
        const size_t chunk_size = 8192;
        uint8_t buffer[chunk_size];
        while (true) {
            size_t r = dstream->read(buffer, chunk_size);
            if (r == 0) break;
            result.insert(result.end(), buffer, buffer + r);
        }
        auto re = !dstream->error();
        delete dstream;
        return re;
    }
}

ReadStream* create_decompressor(ReadStream* source) {
    if (!source) return nullptr;
    if (!source->seekable()) return nullptr;
    uint8_t header[4];
    size_t readed = source->read(header, sizeof(header));
    if (!readed) {
        source->close();
        delete source;
        return nullptr;
    }
    if (!source->seek(-((int64_t)readed), SEEK_CUR)) {
        source->close();
        delete source;
        return nullptr;
    }
#if HAVE_ZSTD
    if (readed >= 4 && !memcmp(header, ZSTD_header, 4)) {
        return new ZstdDecompressor(source);
    }
#endif
    return new ZlibDecompressor(source);
}

#pragma once
#include "stream.h"
#include "xp3vfs_config.h"
#include "zlib.h"
#if HAVE_ZSTD
#include "zstd.h"
#endif
#include <vector>
#include <memory>

class ZlibDecompressor : public ReadStream {
public:
    /**
     * @brief Create a ZlibDecompressor
     * @param source The underlying ReadStream (will be closed and deleted when this object is destroyed)
     */
    ZlibDecompressor(ReadStream* source) : source(source) {
        stream.zalloc = Z_NULL;
        stream.zfree = Z_NULL;
        stream.opaque = Z_NULL;
        stream.avail_in = 0;
        stream.next_in = Z_NULL;
        
        if (inflateInit(&stream) != Z_OK) {
            errored = true;
        }
    }
    
    virtual ~ZlibDecompressor() {
        inflateEnd(&stream);
        if (source) {
            source->close();
            delete source;
            source = nullptr;
        }
    }
    
    virtual size_t read(uint8_t* buf, size_t size) {
        if (errored || finished) return 0;
        
        stream.avail_out = size;
        stream.next_out = buf;
        
        while (stream.avail_out > 0) {
            if (stream.avail_in == 0) {
                stream.avail_in = source->read(in_buffer, sizeof(in_buffer));
                if (stream.avail_in == 0) {
                    if (source->error()) {
                        errored = true;
                        return size - stream.avail_out;
                    }
                    break;
                }
                stream.next_in = in_buffer;
            }
            
            int ret = inflate(&stream, Z_NO_FLUSH);
            if (ret == Z_STREAM_END) {
                finished = true;
                break;
            }
            if (ret != Z_OK) {
                errored = true;
                break;
            }
        }
        
        return size - stream.avail_out;
    }
    
    virtual bool seekable() {
        return false;
    }
    
    virtual bool eof() {
        return finished || source->eof();
    }
    
    virtual bool error() {
        return errored || source->error();
    }
    
    virtual bool close() {
        return source->close();
    }
    
private:
    ReadStream* source;
    z_stream stream = {};
    uint8_t in_buffer[8192];
    bool errored = false;
    bool finished = false;
};

#if HAVE_ZSTD
class ZstdDecompressor : public ReadStream {
public:
    /**
     * @brief Create a ZstdDecompressor
     * @param source The underlying ReadStream (will be closed and deleted when this object is destroyed)
     */
    ZstdDecompressor(ReadStream* source) : source(source) {
        dstream = ZSTD_createDStream();
        if (!dstream) {
            errored = true;
            return;
        }
        
        size_t ret = ZSTD_initDStream(dstream);
        if (ZSTD_isError(ret)) {
            errored = true;
        }
    }
    
    virtual ~ZstdDecompressor() {
        if (dstream) {
            ZSTD_freeDStream(dstream);
            dstream = nullptr;
        }
        if (source) {
            source->close();
            delete source;
            source = nullptr;
        }
    }
    virtual size_t read(uint8_t* buf, size_t size) {
        if (errored || finished) return 0;
        ZSTD_outBuffer output = { buf, size, 0 };
        while (output.pos < output.size) {
            if (input.pos >= input.size) {
                input.size = source->read(in_buffer, sizeof(in_buffer));
                input.pos = 0;
                if (input.size == 0) {
                    if (source->error()) {
                        errored = true;
                        return output.pos;
                    }
                    break;
                }
            }
            
            size_t ret = ZSTD_decompressStream(dstream, &output, &input);
            if (ZSTD_isError(ret)) {
                errored = true;
                break;
            }
            if (ret == 0) {
                finished = true;
                break;
            }
        }
        
        return output.pos;
    }
    virtual bool seekable() {
        return false;
    }
    virtual bool eof() {
        return finished || source->eof();
    }
    virtual bool error() {
        return errored || source->error();
    }
    virtual bool close() {
        return source->close();
    }
private:
    ReadStream* source;
    ZSTD_DStream* dstream = nullptr;
    uint8_t in_buffer[8192];
    ZSTD_inBuffer input = { in_buffer, 0, 0 };
    bool errored = false;
    bool finished = false;
};
#endif

bool decompress(ReadStream* source, std::vector<uint8_t>& result, size_t expected_size = 0);
ReadStream* create_decompressor(ReadStream* stream);

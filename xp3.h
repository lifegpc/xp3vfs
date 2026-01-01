#pragma once
#include <stdint.h>
#include "stream.h"
#include <mutex>

inline const char* XP3_MAGIC = "XP3\r\n \n\x1a\x8b\x67\x01";

inline const char* CHUNK_FILE = "File";
inline const char* CHUNK_INFO = "info";
inline const char* CHUNK_SEGM = "segm";
inline const char* CHUNK_ADLR = "adlr";

inline const uint8_t TVP_XP3_INDEX_ENCODE_METHOD_MASK = 0x07;
inline const uint8_t TVP_XP3_INDEX_ENCODE_RAW = 0x00;
inline const uint8_t TVP_XP3_INDEX_ENCODE_ZLIB = 0x01;
inline const uint8_t TVP_XP3_INDEX_CONTINUE = 0x80;

inline const uint32_t TVP_XP3_FILE_PROTECTED = 1 << 31;

inline const uint32_t TVP_XP3_SEGM_ENCODE_METHOD_MASK = 0x07;
inline const uint32_t TVP_XP3_SEGM_ENCODE_RAW = 0x00;
inline const uint32_t TVP_XP3_SEGM_ENCODE_ZLIB = 0x01;

inline const uint64_t TVP_XP3_CURRENT_HEADER_VERSION = 0x17;

struct Segment {
    uint32_t flag;
    uint64_t start; // start offset in the file
    uint64_t original_size;  // original size of the segment
    uint64_t packed_size;    // packed size of the segment
};

class FileEntry {
public:
    std::string filename;
    uint32_t flags;
    uint64_t original_size; // original size of the file
    uint64_t packed_size;  // packed size of the file
    uint32_t adler32; // adler32 checksum of the file
    std::vector<Segment> segments;
};

class Xp3File: public ReadStream {
public:
    Xp3File(FileEntry entry, ReadStream* stream): entry(entry), stream(stream), pos(0) {
        uint64_t pos = 0;
        for (auto& seg : entry.segments) {
            seg_pos.push_back(pos);
            pos += seg.original_size;
        }
    }
    ~Xp3File() {
        if (cache) {
            cache->close();
            delete cache;
            cache = nullptr;
        }
    }
    virtual size_t read(uint8_t* buf, size_t size);
    virtual bool seek(int64_t offset, int whence);
    virtual int64_t tell() {
        return pos;
    }
    virtual bool seekable() {
        return true;
    }
    virtual bool error() {
        return stream->error() || (cache && cache->error());
    }
    virtual bool eof() {
        return pos >= entry.original_size;
    }
    virtual bool close() {
        if (cache) {
            cache->close();
            delete cache;
            cache = nullptr;
        }
        return true;
    }
    uint64_t get_original_size() const {
        return entry.original_size;
    }
private:
    size_t binary_search_pos(uint64_t offset) {
        size_t left = 0;
        size_t right = seg_pos.size();
        while (left < right) {
            size_t mid = (left + right) / 2;
            if (seg_pos[mid] == offset) {
                return mid;
            } else if (seg_pos[mid] < offset) {
                left = mid + 1;
            } else {
                right = mid;
            }
        }
        return left > 0 ? left - 1 : 0;
    }
    FileEntry entry;
    ReadStream* stream;
    std::vector<uint64_t> seg_pos;
    uint64_t pos;
    ReadStream* cache = nullptr;
};

class Xp3Archive: public ReadStream {
public:
    Xp3Archive(const char* filename, bool thread_safety = true) : stream(new FileReadStream(filename)), thread_safety(thread_safety) {}
    Xp3Archive(ReadStream* stream, bool thread_safety = true) : stream(stream), thread_safety(thread_safety) {}
    ~Xp3Archive() {
        if (stream) {
            stream->close();
            delete stream;
            stream = nullptr;
        }
    }
    bool ReadIndex();
    std::vector<FileEntry> files;
    Xp3File* OpenFile(size_t index);
    Xp3File* OpenFile(FileEntry entry);
    uint32_t GetMinorVersion() const {
        return minor_version;
    }
    virtual size_t read(uint8_t* buf, size_t size) {
        if (thread_safety) {
            std::lock_guard<std::mutex> lock(mutex);
            return stream->read(buf, size);
        } else {
            return stream->read(buf, size);
        }
    }
    virtual bool seek(int64_t offset, int whence) {
        if (thread_safety) {
            std::lock_guard<std::mutex> lock(mutex);
            return stream->seek(offset, whence);
        } else {
            return stream->seek(offset, whence);
        }
    }
    virtual int64_t tell() {
        if (thread_safety) {
            std::lock_guard<std::mutex> lock(mutex);
            return stream->tell();
        } else {
            return stream->tell();
        }
    }
    virtual bool seekable() {
        if (thread_safety) {
            std::lock_guard<std::mutex> lock(mutex);
            return stream->seekable();
        } else {
            return stream->seekable();
        }
    }
    virtual bool eof() {
        if (thread_safety) {
            std::lock_guard<std::mutex> lock(mutex);
            return stream->eof();
        } else {
            return stream->eof();
        }
    }
    virtual bool error() {
        if (thread_safety) {
            std::lock_guard<std::mutex> lock(mutex);
            return stream->error();
        } else {
            return stream->error();
        }
    }
    virtual bool close() {
        if (thread_safety) {
            std::lock_guard<std::mutex> lock(mutex);
            return stream->close();
        } else {
            return stream->close();
        }
    }
private:
    bool ReadFileEntry(MemReadStream& stream);
    ReadStream* stream;
    uint32_t minor_version = 0;
    bool thread_safety;
    std::mutex mutex;
};

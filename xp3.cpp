#include "xp3.h"
#include <string.h>
#include <memory>
#include "decompressor.h"
#include "wchar_util.h"
#include "encoding.h"

bool Xp3Archive::ReadIndex() {
    uint8_t magic[11];
    if (!stream->readall(magic)) {
        return false;
    }
    if (memcmp(magic, XP3_MAGIC, 11) != 0) {
        return false;
    }
    uint64_t index_offset;
    if (!stream->readu64(index_offset)) {
        return false;
    }
    if (!stream->seek(index_offset, SEEK_SET)) {
        return false;
    }
    std::vector<uint8_t> index;
    uint8_t index_encode_method;
    if (!stream->readu8(index_encode_method)) {
        return false;
    }
    switch (index_encode_method) {
    case TVP_XP3_INDEX_ENCODE_RAW:
    {
        uint64_t index_size;
        if (!stream->readu64(index_size)) {
            return false;
        }
        index.resize(index_size);
        if (!stream->readall(index)) {
            return false;
        }
        break;
    }
    case TVP_XP3_INDEX_ENCODE_ZLIB:
    {
        uint64_t packed_size;
        if (!stream->readu64(packed_size)) {
            return false;
        }
        uint64_t original_size;
        if (!stream->readu64(original_size)) {
            return false;
        }
        int64_t current_pos = stream->tell();
        if (current_pos < 0) {
            return false;
        }
        ReadStream* region = new ReadStreamRegion(stream, current_pos, current_pos + packed_size);
        if (!decompress(region, index, original_size)) {
            return false;
        }
        break;
    }
    default:
    {
        printf("Unknown index encode method: %u\n", index_encode_method);
        return false;
    }
    }
    MemReadStream index_stream(index);
    while (!index_stream.eof()) {
        uint8_t chunk_type[4];
        if (!index_stream.readall(chunk_type)) {
            return false;
        }
        uint64_t chunk_size;
        if (!index_stream.readu64(chunk_size)) {
            return false;
        }
        if (memcmp(chunk_type, CHUNK_FILE, 4)) {
            printf("Unknown chunk type: %.4s\n", chunk_type);
            return false;
        }
        std::vector<uint8_t> chunk_data(chunk_size);
        if (!index_stream.readall(chunk_data)) {
            return false;
        }
        MemReadStream chunk_stream(chunk_data);
        if (!ReadFileEntry(chunk_stream)) {
            return false;
        }
    }
    return true;
}

bool Xp3Archive::ReadFileEntry(MemReadStream& stream) {
    FileEntry entry;
    while (!stream.eof()) {
        uint8_t chunk_type[4];
        if (!stream.readall(chunk_type)) {
            return false;
        }
        uint64_t chunk_size;
        if (!stream.readu64(chunk_size)) {
            return false;
        }
        std::vector<uint8_t> chunk_data(chunk_size);
        if (!stream.readall(chunk_data)) {
            return false;
        }
        MemReadStream chunk_stream(chunk_data);
        if (!memcmp(chunk_type, CHUNK_INFO, 4)) {
            if (!chunk_stream.readu32(entry.flags)) {
                return false;
            }
            if (!chunk_stream.readu64(entry.original_size)) {
                return false;
            }
            if (!chunk_stream.readu64(entry.packed_size)) {
                return false;
            }
            uint16_t name_length;
            if (!chunk_stream.readu16(name_length)) {
                return false;
            }
            std::vector<uint8_t> name_data(name_length * 2);
            if (!chunk_stream.readall(name_data)) {
                return false;
            }
#if _WIN32
            std::wstring wname((wchar_t*)name_data.data(), name_length);
            if (!wchar_util::wstr_to_str(entry.filename, wname, 65001)) {
                return false;
            }
#else
            std::string name((char*)name_data.data(), name_length * 2);
            if (!encoding::convert(name, entry.filename, "UTF-16LE", "UTF-8")) {
                return false;
            }
#endif
        } else if (!memcmp(chunk_type, CHUNK_ADLR, 4)) {
            if (!chunk_stream.readu32(entry.adler32)) {
                return false;
            }
        } else if (!memcmp(chunk_type, CHUNK_SEGM, 4)) {
            while (!chunk_stream.eof()) {
                Segment seg;
                if (!chunk_stream.readu32(seg.flag)) {
                    return false;
                }
                if (!chunk_stream.readu64(seg.start)) {
                    return false;
                }
                if (!chunk_stream.readu64(seg.original_size)) {
                    return false;
                }
                if (!chunk_stream.readu64(seg.packed_size)) {
                    return false;
                }
                entry.segments.push_back(seg);
            }
        }
    }
    files.push_back(std::move(entry));
    return true;
}

Xp3File* Xp3Archive::OpenFile(size_t index) {
    return new Xp3File(files[index], stream);
}

Xp3File* Xp3Archive::OpenFile(FileEntry entry) {
    return new Xp3File(std::move(entry), stream);
}

size_t Xp3File::read(uint8_t* buf, size_t size) {
    if (!buf) return 0;
    if (pos >= entry.original_size) return 0;
    if (cache) {
        auto readed = cache->read(buf, size);
        if (readed > 0) {
            pos += readed;
            return readed;
        }
        cache->close();
        delete cache;
        cache = nullptr;
    }
    size_t seg_index = binary_search_pos(pos);
    Segment& seg = entry.segments[seg_index];
    uint64_t start_pos = seg.start;
    uint64_t seg_pos = this->seg_pos[seg_index];
    uint64_t skip_pos = this->pos - seg_pos;
    uint64_t read_size = seg.packed_size;
    if (seg.flag == TVP_XP3_SEGM_ENCODE_ZLIB) {
        ReadStream* region = new ReadStreamRegion(stream, start_pos, start_pos + read_size);
        cache = create_decompressor(region);
        if (!cache) return 0;
        if (skip_pos > 0) {
            cache->skip(skip_pos);
        }
        size_t readed = cache->read(buf, size);
        this->pos += readed;
        return readed;
    }
    std::unique_ptr<ReadStream> region(new ReadStreamRegion(stream, start_pos + skip_pos, start_pos + read_size));
    size_t readed = region->read(buf, size);
    this->pos += readed;
    return readed;
}

bool Xp3File::seek(int64_t offset, int whence) {
    uint64_t new_pos = 0;
    if (whence == SEEK_SET) {
        new_pos = offset;
    } else if (whence == SEEK_CUR) {
        new_pos = pos + offset;
    } else if (whence == SEEK_END) {
        new_pos = entry.original_size + offset;
    } else {
        return false;
    }
    if (new_pos > entry.original_size) {
        return false;
    }
    if (cache && new_pos < entry.original_size) {
        size_t old_seg_index = binary_search_pos(pos);
        size_t new_seg_index = binary_search_pos(new_pos);
        if (old_seg_index == new_seg_index) {
            if (new_pos >= pos) {
                cache->skip(new_pos - pos);
            } else {
                cache->close();
                delete cache;
                cache = nullptr;
            }
        } else {
            cache->close();
            delete cache;
            cache = nullptr;
        }
    }
    pos = new_pos;
    return true;
}

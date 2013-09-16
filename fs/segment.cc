/*
 * Copyright (c) 2013 University of California, Los Angeles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Wentao Shang <wentao@cs.ucla.edu>
 *         Qiuhan Ding <dingqiuhan@gmail.com>
 */

#include "segment.h"
#include <ndn.cxx/data.h>
#include <ndn.cxx/common.h>
#include <ndn.cxx/security/exception.h>

#include <ndn.cxx/helpers/uri.h>

#include <iostream>

using namespace std;
using namespace boost;
using namespace ndn;

int read_segment(const char* path, const uint64_t ver, const int seg, char *output, const int limit, const int offset)
{
#ifdef NDNFS_DEBUG
    cout << "read_segment: path=" << path << std::dec << ", ver=" << ver << ", seg=" << seg << ", limit=" << limit << ", offset=" << offset << endl;
#endif

    const char*co_raw;
    int co_size;
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "SELECT * FROM file_segments WHERE path = ? AND version = ? AND segment = ?;", -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, ver);
    sqlite3_bind_int(stmt, 3, seg);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return -1;
    }

    co_raw = (const char*) sqlite3_column_blob(stmt, 3);
    co_size = sqlite3_column_bytes(stmt, 3);

    Ptr<Blob> data_blob = Create<Blob>(co_raw, co_size);
    Ptr<Data> data = Data::decodeFromWire(data_blob);
    const char *content = data->content().buf();

#ifdef NDNFS_DEBUG
    cout << "read_segment: raw data is " << endl;
    for (int i = 0; i < co_size; i++) {
        cout << co_raw[i];
    }
    cout << endl;
    cout << "read_segment: raw data length is " << co_size << endl;
#endif

    int copy_len = data->content().size();
    if (copy_len > limit)  // Don't write across the limit
        copy_len = limit;

#ifdef NDNFS_DEBUG
    cout << "read_segment: content to copy is " << endl;
    for (int i = 0; i < copy_len; i++)
        cout << content[offset + i];
    cout << endl;
    cout << "read_segment: copy length is " << copy_len << endl;
#endif

    memcpy(output, content + offset, copy_len);
    
    sqlite3_finalize(stmt);
    
    return copy_len;
}


int make_segment(const char* path, const uint64_t ver, const int seg, const bool final, const char *data, const int len)
{
#ifdef NDNFS_DEBUG
    cout << "make_segment: path=" << path << std::dec << ", ver=" << ver << ", seg=" << seg << ", len=" << len << endl;
#endif

    assert(len > 0);

    string file_path(path);
    string full_name = ndnfs::global_prefix + file_path;
    string escaped_name;
    Uri::toEscaped(full_name.begin(), full_name.end(), back_inserter(escaped_name));

    Name seg_name(escaped_name);
    seg_name.appendVersion(ver);
    seg_name.appendSeqNum(seg);
#ifdef NDNFS_DEBUG
    cout << "make_segment: segment name is " << seg_name.toUri() << endl;
#endif

    Content co(data,len);
    Data data0;
    data0.setName(seg_name);
    data0.setContent(co);
    try{
        keychain->sign(data0,signer);
    } catch(security::SecException & e) {
        cerr << e.Msg() << endl;
        cerr << data0.getName() << endl;
    }
    Ptr<Blob> wire_data = data0.encodeToWire();
    char* co_raw = wire_data->buf();
    int co_size = wire_data->size();

#ifdef NDNFS_DEBUG
    cout << "make_segment: raw data is" << endl;
    for (int i = 0; i < co_size; i++) {
        cout << co_raw[i];
    }
    cout << endl;
    cout << "make_segment: raw data length is " << co_size << endl;
#endif

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "INSERT INTO file_segments (path,version,segment,data,offset) VALUES (?,?,?,?,?);", -1, &stmt, 0);
    sqlite3_bind_text(stmt,1,path,-1,SQLITE_STATIC);
    sqlite3_bind_int64(stmt,2,ver);
    sqlite3_bind_int(stmt,3,seg);
    sqlite3_bind_blob(stmt,4,co_raw,co_size,SQLITE_STATIC);
    sqlite3_bind_int(stmt,5,segment_to_size(seg));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return 0;
}

void remove_segments(const char* path, const uint64_t ver, const int start/* = 0 */)
{
#ifdef NDNFS_DEBUG
    cout << "remove_segments: path=" << path << std::dec << ", ver=" << ver << ", from segment #" << start << endl;
#endif

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "SELECT totalSegments FROM file_versions WHERE path = ? AND version = ?;", -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, ver);
    int res = sqlite3_step(stmt);
    if (res != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return;
    }
    int segs = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    for (int i = start; i < segs; i++) {
        sqlite3_prepare_v2(db, "DELETE FROM file_segments WHERE path = ? AND version = ? AND segment = ?;", -1, &stmt, 0);
        sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 2, ver);
        sqlite3_bind_int(stmt, 3, i);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void truncate_segment(const char* path, const uint64_t ver, const int seg, const off_t length)
{
#ifdef NDNFS_DEBUG
    cout << "truncate_segment: path=" << path << std::dec << ", ver=" << ver << ", seg=" << seg << ", length=" << length << endl;
#endif

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "SELECT * FROM file_segments WHERE path = ? AND version = ? AND segment = ?;", -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, ver);
    sqlite3_bind_int(stmt, 3, seg);
    if(sqlite3_step(stmt) == SQLITE_ROW) {
        if (length == 0) {
            sqlite3_finalize(stmt);
            sqlite3_prepare_v2(db, "DELETE FROM file_segments WHERE path = ? AND version = ? AND segment = ?;", -1, &stmt, 0);
            sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
            sqlite3_bind_int64(stmt, 2, ver);
            sqlite3_bind_int(stmt, 3, seg);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        } else {
            const char* co_raw = (const char *)sqlite3_column_blob(stmt, 3);
            int co_size = sqlite3_column_bytes(stmt, 3);

            assert(co_size > (int)length);

            Ptr<Blob> data_blob = Create<Blob>(co_raw, co_size);
            Ptr<Data> data = Data::decodeFromWire(data_blob);
            const Blob& data_content = data->content();
            const char *content = data_content.buf();

            Content co(content, length);
            Data trunc_data;
            trunc_data.setName(data->getName());
            trunc_data.setContent(co);
            keychain->sign(trunc_data,signer);
            Ptr<Blob> wire_data = trunc_data.encodeToWire();
            char *trunc_co_raw = wire_data->buf();
            int trunc_co_size = wire_data->size();

            sqlite3_finalize(stmt);
            sqlite3_prepare_v2(db, "UPDATE file_segments SET data = ? WHERE path = ? AND version = ? AND segment = ?;", -1, &stmt, 0);
            sqlite3_bind_blob(stmt, 1, trunc_co_raw, trunc_co_size, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, path, -1, SQLITE_STATIC);
            sqlite3_bind_int64(stmt, 3, ver);
            sqlite3_bind_int(stmt, 4, seg);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
}

// Copyright (C) 2019-2020 Zilliz. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance
// with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations under the License.

#include "db/Utils.h"

#include <fiu/fiu-local.h>

#include <unistd.h>
#include <boost/filesystem.hpp>
#include <chrono>
#include <memory>
#include <mutex>
#include <regex>
#include <vector>

#include "cache/CpuCacheMgr.h"
#include "db/Types.h"

#ifdef MILVUS_GPU_VERSION

#include "cache/GpuCacheMgr.h"

#endif

#include "config/ServerConfig.h"
//#include "storage/s3/S3ClientWrapper.h"
#include "knowhere/index/vector_index/helpers/IndexParameter.h"
#include "utils/CommonUtil.h"
#include "utils/Log.h"

#include <map>

namespace milvus {
namespace engine {
namespace utils {

int64_t
GetMicroSecTimeStamp() {
    auto now = std::chrono::system_clock::now();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();

    return micros;
}

bool
IsSameIndex(const CollectionIndex& index1, const CollectionIndex& index2) {
    return index1.index_type_ == index2.index_type_ && index1.extra_params_ == index2.extra_params_ &&
           index1.metric_name_ == index2.metric_name_;
}

bool
IsBinaryMetricType(const std::string& metric_type) {
    return (metric_type == knowhere::Metric::HAMMING) || (metric_type == knowhere::Metric::JACCARD) ||
           (metric_type == knowhere::Metric::SUBSTRUCTURE) || (metric_type == knowhere::Metric::SUPERSTRUCTURE) ||
           (metric_type == knowhere::Metric::TANIMOTO);
}

engine::date_t
GetDate(const std::time_t& t, int day_delta) {
    struct tm ltm;
    localtime_r(&t, &ltm);
    if (day_delta > 0) {
        do {
            ++ltm.tm_mday;
            --day_delta;
        } while (day_delta > 0);
        mktime(&ltm);
    } else if (day_delta < 0) {
        do {
            --ltm.tm_mday;
            ++day_delta;
        } while (day_delta < 0);
        mktime(&ltm);
    }
    return ltm.tm_year * 10000 + ltm.tm_mon * 100 + ltm.tm_mday;
}

engine::date_t
GetDateWithDelta(int day_delta) {
    return GetDate(std::time(nullptr), day_delta);
}

engine::date_t
GetDate() {
    return GetDate(std::time(nullptr), 0);
}

// URI format: dialect://username:password@host:port/database
Status
ParseMetaUri(const std::string& uri, MetaUriInfo& info) {
    std::string dialect_regex = "(.*)";
    std::string username_tegex = "(.*)";
    std::string password_regex = "(.*)";
    std::string host_regex = "(.*)";
    std::string port_regex = "(.*)";
    std::string db_name_regex = "(.*)";
    std::string uri_regex_str = dialect_regex + R"(\:\/\/)" + username_tegex + R"(\:)" + password_regex + R"(\@)" +
                                host_regex + R"(\:)" + port_regex + R"(\/)" + db_name_regex;

    std::regex uri_regex(uri_regex_str);
    std::smatch pieces_match;

    if (std::regex_match(uri, pieces_match, uri_regex)) {
        info.dialect_ = pieces_match[1].str();
        info.username_ = pieces_match[2].str();
        info.password_ = pieces_match[3].str();
        info.host_ = pieces_match[4].str();
        info.port_ = pieces_match[5].str();
        info.db_name_ = pieces_match[6].str();

        // TODO(myh): verify host, port...
    } else {
        return Status(DB_INVALID_META_URI, "Invalid meta uri: " + uri);
    }

    return Status::OK();
}

void
SendExitSignal() {
    LOG_SERVER_INFO_ << "Send SIGUSR2 signal to exit";
    pid_t pid = getpid();
    kill(pid, SIGUSR2);
}

void
GetIDFromChunk(const engine::DataChunkPtr& chunk, engine::IDNumbers& ids) {
    ids.clear();
    if (chunk == nullptr) {
        return;
    }

    auto pair = chunk->fixed_fields_.find(engine::FIELD_UID);
    if (pair == chunk->fixed_fields_.end() || pair->second == nullptr) {
        return;
    }

    if (!pair->second->data_.empty()) {
        ids.resize(pair->second->data_.size() / sizeof(engine::idx_t));
        memcpy(ids.data(), pair->second->data_.data(), pair->second->data_.size());
    }
}

int64_t
GetSizeOfChunk(const engine::DataChunkPtr& chunk) {
    if (chunk == nullptr) {
        return 0;
    }

    int64_t total_size = 0;
    for (auto& pair : chunk->fixed_fields_) {
        if (pair.second == nullptr) {
            continue;
        }
        total_size += pair.second->Size();
    }
    for (auto& pair : chunk->variable_fields_) {
        if (pair.second == nullptr) {
            continue;
        }
        total_size += pair.second->Size();
    }

    return total_size;
}

bool
RequireRawFile(const std::string& index_type) {
    return index_type == knowhere::IndexEnum::INDEX_FAISS_IVFFLAT || index_type == knowhere::IndexEnum::INDEX_NSG ||
           index_type == knowhere::IndexEnum::INDEX_HNSW;
}

bool
RequireCompressFile(const std::string& index_type) {
    return index_type == knowhere::IndexEnum::INDEX_RHNSWSQ;
}

}  // namespace utils
}  // namespace engine
}  // namespace milvus

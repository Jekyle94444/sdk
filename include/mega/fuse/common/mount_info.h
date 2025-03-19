#pragma once

#include <mega/fuse/common/mount_flags.h>
#include <mega/fuse/common/mount_info_forward.h>
#include <mega/fuse/common/normalized_path.h>
#include <mega/fuse/common/query_forward.h>
#include <mega/fuse/common/scoped_query_forward.h>

#include <mega/types.h>

namespace mega
{
namespace fuse
{

struct MountInfo
{
    bool operator==(const MountInfo& rhs) const;

    bool operator!=(const MountInfo& info) const;

    static MountInfo deserialize(Query& query);
    static MountInfo deserialize(ScopedQuery& query);

    // Convenience.
    void name(const std::string& name);

    const std::string& name() const;

    void serialize(Query& query) const;
    void serialize(ScopedQuery& query) const;

    MountFlags mFlags;
    NodeHandle mHandle;
    NormalizedPath mPath;
}; // MountInfo

struct MountInfoNameLess
{
    bool operator()(const MountInfo& lhs, const MountInfo& rhs) const;
}; // MountInfoNameLess

struct MountInfoPathLess
{
    bool operator()(const MountInfo& lhs, const MountInfo& rhs) const;
}; // MountInfoPathLess

} // fuse
} // mega


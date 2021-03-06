#pragma once

#include <DataStreams/IBlockInputStream.h>
#include <Storages/MergeTree/MergeTreeBlockReadUtils.h>
#include <Storages/MergeTree/MergeTreeData.h>
#include <Storages/SelectQueryInfo.h>

#include <Processors/Sources/SourceWithProgress.h>

namespace DB
{

class MergeTreeReader;
class UncompressedCache;
class MarkCache;


/// Base class for MergeTreeThreadSelectProcessor and MergeTreeSelectProcessor
class MergeTreeBaseSelectProcessor : public SourceWithProgress
{
public:
    MergeTreeBaseSelectProcessor(
        Block header,
        const MergeTreeData & storage_,
        const PrewhereInfoPtr & prewhere_info_,
        UInt64 max_block_size_rows_,
        UInt64 preferred_block_size_bytes_,
        UInt64 preferred_max_column_in_block_size_bytes_,
        UInt64 min_bytes_to_use_direct_io_,
        UInt64 min_bytes_to_use_mmap_io_,
        UInt64 max_read_buffer_size_,
        bool use_uncompressed_cache_,
        bool save_marks_in_cache_ = true,
        const Names & virt_column_names_ = {});

    ~MergeTreeBaseSelectProcessor() override;

    static void executePrewhereActions(Block & block, const PrewhereInfoPtr & prewhere_info);

protected:
    Chunk generate() final;

    /// Creates new this->task, and initializes readers.
    virtual bool getNewTask() = 0;

    virtual Chunk readFromPart();

    Chunk readFromPartImpl();

    /// Two versions for header and chunk.
    static void injectVirtualColumns(Block & block, MergeTreeReadTask * task, const Names & virtual_columns);
    static void injectVirtualColumns(Chunk & chunk, MergeTreeReadTask * task, const Names & virtual_columns);

    static Block getHeader(Block block, const PrewhereInfoPtr & prewhere_info, const Names & virtual_columns);

    void initializeRangeReaders(MergeTreeReadTask & task);

protected:
    const MergeTreeData & storage;

    PrewhereInfoPtr prewhere_info;

    UInt64 max_block_size_rows;
    UInt64 preferred_block_size_bytes;
    UInt64 preferred_max_column_in_block_size_bytes;

    UInt64 min_bytes_to_use_direct_io;
    UInt64 min_bytes_to_use_mmap_io;
    UInt64 max_read_buffer_size;

    bool use_uncompressed_cache;
    bool save_marks_in_cache;

    Names virt_column_names;
    /// This header is used for chunks from readFromPart().
    Block header_without_virtual_columns;

    std::unique_ptr<MergeTreeReadTask> task;

    std::shared_ptr<UncompressedCache> owned_uncompressed_cache;
    std::shared_ptr<MarkCache> owned_mark_cache;

    using MergeTreeReaderPtr = std::unique_ptr<MergeTreeReader>;
    MergeTreeReaderPtr reader;
    MergeTreeReaderPtr pre_reader;
};

}

#ifndef SERIALIZE_H
#define SERIALIZE_H

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/list.h>

#define FILENAME_PREFIX "data"

// 序列化单个核心的 completed_records
static inline int serialize_core_records(
    const char* data_dir, int core_id, struct list_head* completed_records) {
  struct file* filp;
  loff_t pos = 0;
  char filename[256];
  struct profile_record* record;
  int ret = 0;

  // 跳过空链表
  if (list_empty(completed_records)) return 0;

  // 生成文件名：data_dir/dataX
  snprintf(filename, sizeof(filename), "%s/%s%d", data_dir, FILENAME_PREFIX,
           core_id);

  // 打开文件
  filp = filp_open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (IS_ERR(filp)) return PTR_ERR(filp);

  // 写入魔数和版本号
  u32 magic = 0x434F5245; // 'CORE'
  u32 version = 1;
  ret = kernel_write(filp, &magic, sizeof(magic), &pos);
  if (ret < 0) goto error;

  ret = kernel_write(filp, &version, sizeof(version), &pos);
  if (ret < 0) goto error;

  // 遍历链表并写入数据
  list_for_each_entry(record, completed_records, list) {
    // 写入 metadata（跳过 req 指针）
    ret = kernel_write(filp, &record->metadata,
                       offsetof(struct profile_record, metadata.req), &pos);
    if (ret < 0) goto error;

    // 写入时间戳和事件
    ret = kernel_write(filp, record->timestamps, sizeof(u64) * MAX_EVENT_NUM,
                       &pos);
    if (ret < 0) goto error;
    ret = kernel_write(filp, record->events,
                       sizeof(enum EEvent) * MAX_EVENT_NUM, &pos);
    if (ret < 0) goto error;

    // 写入其他字段
    ret = kernel_write(filp, &record->cnt, sizeof(size_t), &pos);
    if (ret < 0) goto error;
    ret = kernel_write(filp, &record->stat1, sizeof(struct ntprof_stat), &pos);
    if (ret < 0) goto error;
    ret = kernel_write(filp, &record->stat2, sizeof(struct ntprof_stat), &pos);
    if (ret < 0) goto error;
  }

error:
  filp_close(filp, NULL);
  return ret;
}

int serialize_all_cores(const char* data_dir,
                        struct per_core_statistics* stats) {
  int ret;
  for (int i = 0; i < MAX_CORE_NUM; i++) {
    ret = serialize_core_records(data_dir, i, &stats[i].completed_records);
    if (ret < 0) break;
  }
  return ret;
}


struct read_dir_context {
  struct dir_context ctx;
  char filenames[MAX_CORE_NUM][256]; // 假设最多MAX_CORE_NUM个文件
  int count;
};

static int iterate_dir_actor(struct dir_context* ctx, const char* name,
                             int namlen, loff_t offset, u64 ino,
                             unsigned int d_type) {
  struct read_dir_context* rctx = container_of(ctx, struct read_dir_context,
                                               ctx);

  if (rctx->count >= MAX_CORE_NUM) // 最多只保存MAX_CORE_NUM个文件名
    return -1;

  if (strncmp(name, ".", 1) == 0 || strncmp(name, "..", 2) == 0)
    return 0; // 跳过.和..

  snprintf(rctx->filenames[rctx->count], sizeof(rctx->filenames[rctx->count]),
           "%s", name);
  rctx->count++;

  return 0;
}

static int deserialize_core_records(
    const char* filename,
    int core_id,
    struct per_core_statistics* stats
    ) {
  struct file* filp;
  loff_t pos = 0;
  struct profile_record* record;
  u32 magic, version;
  int ret;

  filp = filp_open(filename, O_RDONLY, 0);
  if (IS_ERR(filp)) return PTR_ERR(filp);

  // 验证魔数和版本
  ret = kernel_read(filp, &magic, sizeof(magic), &pos);
  if (ret != sizeof(magic) || magic != 0x434F5245) goto error;
  ret = kernel_read(filp, &version, sizeof(version), &pos);
  if (ret != sizeof(version) || version != 1) goto error;

  // 清空原有链表（如果需要）
  INIT_LIST_HEAD(&stats[core_id].completed_records);

  while (1) {
    record = kzalloc(sizeof(*record), GFP_KERNEL);
    if (!record) {
      ret = -ENOMEM;
      break;
    }
    INIT_LIST_HEAD(&record->list);

    // 读取 metadata（跳过 req 指针）
    ret = kernel_read(filp, &record->metadata,
                      offsetof(struct profile_record, metadata.req),
                      &pos);
    if (ret < 0) break;

    // 读取时间戳和事件
    ret = kernel_read(filp, record->timestamps, sizeof(u64) * MAX_EVENT_NUM,
                      &pos);
    if (ret < 0) break;
    ret = kernel_read(filp, record->events, sizeof(enum EEvent) * MAX_EVENT_NUM,
                      &pos);
    if (ret < 0) break;

    // 读取其他字段
    ret = kernel_read(filp, &record->cnt, sizeof(size_t), &pos);
    if (ret < 0) break;
    ret = kernel_read(filp, &record->stat1, sizeof(struct ntprof_stat), &pos);
    if (ret < 0) break;
    ret = kernel_read(filp, &record->stat2, sizeof(struct ntprof_stat), &pos);
    if (ret < 0) break;

    // 添加到链表
    list_add_tail(&record->list, &stats[core_id].completed_records);
  }

  if (ret == 0) ret = 0; // 正常结束
  else kfree(record); // 清理最后一个未完成记录

error:
  filp_close(filp, NULL);
  return ret;
}

int deserialize_all_cores(const char* data_dir,
                          struct per_core_statistics* stats) {
  struct file* dir_filp;
  struct read_dir_context ctx = {
      .ctx.actor = iterate_dir_actor,
      .count = 0
  };
  char path[256];
  int ret = 0;

  snprintf(path, sizeof(path), "%s", data_dir);
  dir_filp = filp_open(path, O_RDONLY | O_DIRECTORY, 0);
  if (IS_ERR(dir_filp)) {
    return PTR_ERR(dir_filp);
  }

  // 使用 iterate_dir 读取目录内容
  ret = iterate_dir(dir_filp, &ctx.ctx);
  if (ret < 0) {
    filp_close(dir_filp, NULL);
    return ret;
  }

  // 遍历保存下来的文件名，逐个处理
  for (int i = 0; i < ctx.count; i++) {
    char filename[256];
    int core_id;

    if (sscanf(ctx.filenames[i], FILENAME_PREFIX "%d", &core_id) != 1)
      continue;

    if (core_id < 0 || core_id >= MAX_CORE_NUM)
      continue;

    snprintf(filename, sizeof(filename), "%s/%s", data_dir, ctx.filenames[i]);

    ret = deserialize_core_records(filename, core_id, stats);
    if (ret < 0)
      break;
  }

  filp_close(dir_filp, NULL);
  return ret;
}

#endif //SERIALIZE_H
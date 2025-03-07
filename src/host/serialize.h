#ifndef SERIALIZE_H
#define SERIALIZE_H

#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/namei.h>
#include <linux/path.h>
#include <linux/slab.h>
#include <linux/vfs.h>


#define FILENAME_PREFIX "data"


/**
 * 序列化单个 profile_record 到已打开的文件
 * @param filp  已打开的文件指针
 * @param pos   文件偏移量指针（会更新）
 * @param record 要序列化的记录
 * @return      成功返回 0，失败返回错误码
 */
static inline int serialize_profile_record(struct file* filp, loff_t* pos,
                            struct profile_record* record) {
  int ret;
  size_t metadata_size = sizeof(record->metadata) - sizeof(record->metadata.req);

  // 写入 metadata（跳过 req 指针）
  ret = kernel_write(filp, &record->metadata.disk, metadata_size, pos);
  if (ret < 0) return ret;
  if (ret != metadata_size) return -EIO;

  // 写入时间戳和事件
  ret = kernel_write(filp, record->timestamps, sizeof(u64) * MAX_EVENT_NUM, pos);
  if (ret != sizeof(u64) * MAX_EVENT_NUM) return -EIO;

  ret = kernel_write(filp, record->events, sizeof(enum EEvent) * MAX_EVENT_NUM, pos);
  if (ret != sizeof(enum EEvent) * MAX_EVENT_NUM) return -EIO;

  // 写入 cnt、stat1、stat2
  ret = kernel_write(filp, &record->cnt, sizeof(size_t), pos);
  if (ret != sizeof(size_t)) return -EIO;

  ret = kernel_write(filp, &record->stat1, sizeof(struct ntprof_stat), pos);
  if (ret != sizeof(struct ntprof_stat)) return -EIO;

  ret = kernel_write(filp, &record->stat2, sizeof(struct ntprof_stat), pos);
  if (ret != sizeof(struct ntprof_stat)) return -EIO;

  return 0;
}

/**
 * 从文件反序列化单个 profile_record
 * @param filp   已打开的文件指针
 * @param pos    文件偏移量指针（会更新）
 * @param record 输出记录（需提前分配内存）
 * @return       成功返回 0，失败返回错误码
 */
static inline int deserialize_profile_record(struct file* filp, loff_t* pos,
                                             struct profile_record* record) {
  int ret;
  // 计算 metadata 中需要反序列化的部分大小（与序列化逻辑严格一致）
  size_t metadata_size = sizeof(record->metadata) - sizeof(record->metadata.req);

  // 1. 读取 metadata（跳过 req 指针）
  ret = kernel_read(filp, &record->metadata.disk, metadata_size, pos);
  if (ret == 0) return -ENODATA;  // 明确表示文件结束
  if (ret < 0) return ret;         // 底层错误（如 -EFAULT）
  if (ret != metadata_size) return -EIO;  // 数据长度不匹配

  // 2. 读取时间戳数组
  ret = kernel_read(filp, record->timestamps, sizeof(u64) * MAX_EVENT_NUM, pos);
  if (ret < 0) return ret;
  if (ret != sizeof(u64) * MAX_EVENT_NUM) return -EIO;

  // 3. 读取事件枚举数组
  ret = kernel_read(filp, record->events, sizeof(enum EEvent) * MAX_EVENT_NUM, pos);
  if (ret < 0) return ret;
  if (ret != sizeof(enum EEvent) * MAX_EVENT_NUM) return -EIO;

  // 4. 读取 cnt 字段
  ret = kernel_read(filp, &record->cnt, sizeof(size_t), pos);
  if (ret < 0) return ret;
  if (ret != sizeof(size_t)) return -EIO;

  // 5. 读取 stat1
  ret = kernel_read(filp, &record->stat1, sizeof(struct ntprof_stat), pos);
  if (ret < 0) return ret;
  if (ret != sizeof(struct ntprof_stat)) return -EIO;

  // 6. 读取 stat2
  ret = kernel_read(filp, &record->stat2, sizeof(struct ntprof_stat), pos);
  if (ret < 0) return ret;
  if (ret != sizeof(struct ntprof_stat)) return -EIO;

  return 0;
}


static inline int serialize_profile_records(struct list_head* completed_records,
                             const char* data_dir, int core_id) {
  struct file* filp;
  loff_t pos = 0;
  char filename[256];
  struct profile_record* record;
  int ret = 0;

  if (list_empty(completed_records)) return 0;

  // 生成文件名：data_dir/dataX
  snprintf(filename, sizeof(filename), "%s/%s%d", data_dir, FILENAME_PREFIX, core_id);
  filp = filp_open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (IS_ERR(filp)) return PTR_ERR(filp);

  pr_info("start writing data to file: %s", filename);

  // 写入魔数和版本号
  u32 magic = 0x434F5245;  // 'CORE'
  u32 version = 1;
  ret = kernel_write(filp, &magic, sizeof(magic), &pos);
  if (ret < 0) goto error;

  ret = kernel_write(filp, &version, sizeof(version), &pos);
  if (ret < 0) goto error;

  // 遍历链表，逐个序列化记录
  int cnt = 0;
  list_for_each_entry(record, completed_records, list) {
    ret = serialize_profile_record(filp, &pos, record);
    cnt++;
    if (ret < 0) break;
  }

  pr_info("write %d records to file: %s", cnt, filename);

  error:
    filp_close(filp, NULL);
  return ret;
}

static inline int serialize_all_cores(const char* data_dir,
                        struct per_core_statistics* stats) {
  int ret = 0;
  for (int i = 0; i < MAX_CORE_NUM; i++) {
    ret = serialize_profile_records(&stats[i].completed_records, data_dir, i);
    if (ret < 0) break;
  }
  pr_info("finished serializing core records to files, ret=%d\n", ret);
  return ret;
}

struct read_dir_context {
  struct dir_context ctx;
  char filenames[MAX_CORE_NUM][256];  // 假设最多MAX_CORE_NUM个文件
  int count;
};

static int iterate_dir_actor(struct dir_context* ctx, const char* name,
                             int namlen, loff_t offset, u64 ino,
                             unsigned int d_type) {
  struct read_dir_context* rctx =
      container_of(ctx, struct read_dir_context, ctx);

  // 跳过非普通文件
  if (d_type != DT_REG) return 0;

  // 跳过 "." 和 ".."
  if (strncmp(name, ".", 1) == 0 || strncmp(name, "..", 2) == 0) return 0;

  // 检查文件数量限制
  if (rctx->count >= MAX_CORE_NUM) return -ENOSPC;

  // 保存文件名
  strncpy(rctx->filenames[rctx->count], name, sizeof(rctx->filenames[0]) - 1);
  rctx->filenames[rctx->count][sizeof(rctx->filenames[0]) - 1] = '\0';
  rctx->count++;

  return 0;
}

/**
 * 从核心文件反序列化整个链表的记录
 * @param data_dir   数据目录（如 "/data/ntprof"）
 * @param core_id    核心ID（用于生成文件名）
 * @param stats      目标统计数据结构
 * @return           成功返回 0，失败返回错误码
 */
static inline int deserialize_core_records(const char* data_dir, int core_id,
                            struct per_core_statistics* stats) {
  struct file* filp;
  loff_t pos = 0;
  char filename[256];
  int ret;
  u32 magic, version;

  // 生成文件名：data_dir/dataX
  snprintf(filename, sizeof(filename), "%s/%s%d", data_dir, FILENAME_PREFIX, core_id);
  pr_info("deserialize_core_records: %s\n", filename);
  filp = filp_open(filename, O_RDONLY, 0);
  if (IS_ERR(filp)) return PTR_ERR(filp);

  // 验证魔数和版本号
  ret = kernel_read(filp, &magic, sizeof(magic), &pos);
  if (ret != sizeof(magic) || magic != 0x434F5245) goto error;
  ret = kernel_read(filp, &version, sizeof(version), &pos);
  if (ret != sizeof(version) || version != 1) goto error;

  // 清空原有链表（根据需求可选）
  INIT_LIST_HEAD(&stats[core_id].completed_records);

  pr_info("start deserializing data from file: %s", filename);
  int cnt = 0;

  // 循环读取记录直到文件结束
  while (1) {
    struct profile_record* record = kzalloc(sizeof(*record), GFP_KERNEL);
    if (!record) {
      ret = -ENOMEM;
      goto error;
    }
    INIT_LIST_HEAD(&record->list);

    // 调用核心反序列化函数
    ret = deserialize_profile_record(filp, &pos, record);
    if (ret == -ENODATA) {  // 文件正常结束
      kfree(record);
      ret = 0;
      break;
    } else if (ret < 0) {   // 其他错误
      kfree(record);
      break;
    }

    // 添加到链表
    list_add_tail(&record->list, &stats[core_id].completed_records);
    cnt++;
  }

  pr_info("after deserializing %d records from file: %s", cnt, filename);

  // 处理正常结束（ret == 0）
  if (ret == -EIO && pos == filp->f_inode->i_size) ret = 0;

  error:
    filp_close(filp, NULL);
  return ret;
}

int deserialize_all_cores(const char* data_dir,
                          struct per_core_statistics* stats) {
  struct file* dir_filp;
  struct read_dir_context ctx = {.ctx.actor = iterate_dir_actor, .count = 0};
  char path[256];
  int ret = 0;
  int last_error = 0;

  snprintf(path, sizeof(path), "%s", data_dir);
  dir_filp = filp_open(path, O_RDONLY | O_DIRECTORY, 0);
  if (IS_ERR(dir_filp)) {
    return PTR_ERR(dir_filp);
  }

  pr_info("dir_path=%s\n", path);

  // 读取目录内容
  ret = iterate_dir(dir_filp, &ctx.ctx);
  if (ret < 0 && ret != -ENOSPC) { // 允许超出 MAX_CORE_NUM 的错误
    filp_close(dir_filp, NULL);
    return ret;
  }

  pr_info("found %d files in directory: %s\n", ctx.count, path);

  // 遍历文件名
  for (int i = 0; i < ctx.count; i++) {
    int core_id;
    size_t prefix_len = strlen(FILENAME_PREFIX);

    // 严格匹配文件名格式: FILENAME_PREFIX + 数字
    if (strncmp(ctx.filenames[i], FILENAME_PREFIX, prefix_len) != 0) continue;
    if (kstrtoint(ctx.filenames[i] + prefix_len, 10, &core_id) != 0) continue;
    if (core_id < 0 || core_id >= MAX_CORE_NUM) continue;

    // 构建完整路径
    pr_info("get the dir path: %s, id: %d\n", data_dir, core_id);

    // 反序列化单个核心（记录错误但继续执行）
    ret = deserialize_core_records(data_dir, core_id, stats);
    if (ret < 0) last_error = ret;
  }

  filp_close(dir_filp, NULL);
  return last_error; // 返回最后一个错误（0 表示全部成功）
}

#endif  // SERIALIZE_H
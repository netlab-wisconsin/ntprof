#ifndef SERIALIZE_H
#define SERIALIZE_H

#include <linux/fs.h>
#include <linux/list.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <linux/vfs.h>


#define FILENAME_PREFIX "data"

/**
 * Serialize a single profile record to a file.
 * @param filp
 * @param pos
 * @param record
 * @return
 */
static inline int serialize_profile_record(struct file* filp, loff_t* pos,
                            struct profile_record* record) {
  int ret;

  // the struct request *req is not serialized
  size_t metadata_size = sizeof(record->metadata) - sizeof(record->metadata.req);

  ret = kernel_write(filp, &record->metadata.disk, metadata_size, pos);
  if (ret < 0) return ret;
  if (ret != metadata_size) return -EIO;

  ret = kernel_write(filp, record->timestamps, sizeof(u64) * MAX_EVENT_NUM, pos);
  if (ret != sizeof(u64) * MAX_EVENT_NUM) return -EIO;

  ret = kernel_write(filp, record->events, sizeof(enum EEvent) * MAX_EVENT_NUM, pos);
  if (ret != sizeof(enum EEvent) * MAX_EVENT_NUM) return -EIO;

  ret = kernel_write(filp, &record->cnt, sizeof(size_t), pos);
  if (ret != sizeof(size_t)) return -EIO;

  ret = kernel_write(filp, &record->stat1, sizeof(struct ntprof_stat), pos);
  if (ret != sizeof(struct ntprof_stat)) return -EIO;

  ret = kernel_write(filp, &record->stat2, sizeof(struct ntprof_stat), pos);
  if (ret != sizeof(struct ntprof_stat)) return -EIO;

  return 0;
}

/**
 * Deserialize a single profile record from a file.
 *
 * @param filp
 * @param pos
 * @param record
 * @return
 */
static inline int deserialize_profile_record(struct file* filp, loff_t* pos,
                                             struct profile_record* record) {
  int ret;

  size_t metadata_size = sizeof(record->metadata) - sizeof(record->metadata.req);

  // 1. Read the metadata, skip the req field
  ret = kernel_read(filp, &record->metadata.disk, metadata_size, pos);
  if (ret == 0) return -ENODATA;  // the file ends
  if (ret < 0) return ret;         // error
  if (ret != metadata_size) return -EIO;  // mismatched size

  // 2. read timestamps
  ret = kernel_read(filp, record->timestamps, sizeof(u64) * MAX_EVENT_NUM, pos);
  if (ret < 0) return ret;
  if (ret != sizeof(u64) * MAX_EVENT_NUM) return -EIO;

  // 3. read events
  ret = kernel_read(filp, record->events, sizeof(enum EEvent) * MAX_EVENT_NUM, pos);
  if (ret < 0) return ret;
  if (ret != sizeof(enum EEvent) * MAX_EVENT_NUM) return -EIO;

  // 4. read cnt
  ret = kernel_read(filp, &record->cnt, sizeof(size_t), pos);
  if (ret < 0) return ret;
  if (ret != sizeof(size_t)) return -EIO;

  // 5. read stat1
  ret = kernel_read(filp, &record->stat1, sizeof(struct ntprof_stat), pos);
  if (ret < 0) return ret;
  if (ret != sizeof(struct ntprof_stat)) return -EIO;

  // 6. read stat2
  ret = kernel_read(filp, &record->stat2, sizeof(struct ntprof_stat), pos);
  if (ret < 0) return ret;
  if (ret != sizeof(struct ntprof_stat)) return -EIO;

  return 0;
}

/**
 * Serialize a list of profile records to a file.
 * @param completed_records
 * @param data_dir
 * @param core_id
 * @return
 */
static inline int serialize_profile_records(struct list_head* completed_records,
                             const char* data_dir, int core_id) {
  struct file* filp;
  loff_t pos = 0;
  char filename[256];
  struct profile_record* record;
  int ret = 0;

  if (list_empty(completed_records)) return 0;

  // Form the filename, data_dir/dataX
  snprintf(filename, sizeof(filename), "%s/%s%d", data_dir, FILENAME_PREFIX, core_id);
  filp = filp_open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (IS_ERR(filp)) return PTR_ERR(filp);

  u32 magic = 0x434F5245;
  u32 version = 1;
  ret = kernel_write(filp, &magic, sizeof(magic), &pos);
  if (ret < 0) goto error;

  ret = kernel_write(filp, &version, sizeof(version), &pos);
  if (ret < 0) goto error;

  // traverse the linked list and serialize each record
  int cnt = 0;
  list_for_each_entry(record, completed_records, list) {
    ret = serialize_profile_record(filp, &pos, record);
    cnt++;
    if (ret < 0) break;
  }

  pr_debug("write %d records to file: %s", cnt, filename);

  error:
    filp_close(filp, NULL);
  return ret;
}

/**
 *
 * Serialize all profile records of all queues.
 *
 * @param data_dir
 * @param stats
 * @return
 */
static inline int serialize_all_cores(const char* data_dir,
                        struct per_core_statistics* stats) {
  int ret = 0;
  for (int i = 0; i < MAX_CORE_NUM; i++) {
    ret = serialize_profile_records(&stats[i].completed_records, data_dir, i);
    if (ret < 0) break;
  }
  return ret;
}

struct read_dir_context {
  struct dir_context ctx;
  char filenames[MAX_CORE_NUM][256]; \
  int count;
};

static int iterate_dir_actor(struct dir_context* ctx, const char* name,
                             int namlen, loff_t offset, u64 ino,
                             unsigned int d_type) {
  struct read_dir_context* rctx =
      container_of(ctx, struct read_dir_context, ctx);

  if (d_type != DT_REG) return 0;

  // skip "." and ".."
  if (strncmp(name, ".", 1) == 0 || strncmp(name, "..", 2) == 0) return 0;

  // check if the count exceeds the limit
  if (rctx->count >= MAX_CORE_NUM) return -ENOSPC;

  // store the filename
  strncpy(rctx->filenames[rctx->count], name, sizeof(rctx->filenames[0]) - 1);
  rctx->filenames[rctx->count][sizeof(rctx->filenames[0]) - 1] = '\0';
  rctx->count++;

  return 0;
}

/**
 * traverse the directory and call the actor function for each file
 * @param data_dir   data directory, such as /data/ntprof_data
 * @param core_id
 * @param stats
 * @return
 */
static inline int deserialize_core_records(const char* data_dir, int id,
                            struct per_core_statistics* stats) {
  struct file* filp;
  loff_t pos = 0;
  char filename[256];
  int ret;
  u32 magic, version;

  // form the file name data_dir/dataX
  snprintf(filename, sizeof(filename), "%s/%s%d", data_dir, FILENAME_PREFIX, id);
  pr_debug("deserialize records in file: %s\n", filename);
  filp = filp_open(filename, O_RDONLY, 0);
  if (IS_ERR(filp)) return PTR_ERR(filp);

  ret = kernel_read(filp, &magic, sizeof(magic), &pos);
  if (ret != sizeof(magic) || magic != 0x434F5245) goto error;
  ret = kernel_read(filp, &version, sizeof(version), &pos);
  if (ret != sizeof(version) || version != 1) goto error;

  // clear the existing list
  INIT_LIST_HEAD(&stats[id].completed_records);

  int cnt = 0;

  // read the file until the end
  while (1) {
    struct profile_record* record = kzalloc(sizeof(*record), GFP_KERNEL);
    if (!record) {
      ret = -ENOMEM;
      goto error;
    }
    INIT_LIST_HEAD(&record->list);

    // deserialize a single profile record
    ret = deserialize_profile_record(filp, &pos, record);
    if (ret == -ENODATA) {  // 文件正常结束
      kfree(record);
      ret = 0;
      break;
    } else if (ret < 0) {   // 其他错误
      kfree(record);
      break;
    }

    // append the deserialized profile record to the list
    list_add_tail(&record->list, &stats[id].completed_records);
    cnt++;
  }

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

  // read the data dir
  ret = iterate_dir(dir_filp, &ctx.ctx);
  if (ret < 0 && ret != -ENOSPC) {
    filp_close(dir_filp, NULL);
    return ret;
  }

  // traverse the all the files in the dir
  for (int i = 0; i < ctx.count; i++) {
    int core_id;
    size_t prefix_len = strlen(FILENAME_PREFIX);

    if (strncmp(ctx.filenames[i], FILENAME_PREFIX, prefix_len) != 0) continue;
    if (kstrtoint(ctx.filenames[i] + prefix_len, 10, &core_id) != 0) continue;
    if (core_id < 0 || core_id >= MAX_CORE_NUM) continue;

    ret = deserialize_core_records(data_dir, core_id, stats);
    if (ret < 0) last_error = ret;
  }

  filp_close(dir_filp, NULL);
  return last_error;
}

#endif  // SERIALIZE_H
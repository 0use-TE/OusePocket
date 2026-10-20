// SPDX-License-Identifier: MIT
#include "ouse_app_files.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ouse_net.h"
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef OUSE_APP_FILES_ROOT
#define OUSE_APP_FILES_ROOT "/sdcard/Ouse/Apps"
#endif
#define FILE_LIMIT (128U * 1024 * 1024)
typedef struct {
  ouse_task_owner_t owner;
  ouse_app_file_operation_t operation;
  char path[OUSE_APP_FILE_PATH], data[OUSE_APP_FILE_CHUNK + 1];
  uint32_t offset, id;
  size_t bytes;
  ouse_app_file_complete_t complete;
  void *context;
  FILE *stream;
  size_t downloaded;
} file_job_t;
static QueueHandle_t jobs;
static bool valid_app(const char *id) {
  if (!id || strncmp(id, "user.", 5) || !id[5] || strlen(id) >= 48 ||
      strstr(id, ".."))
    return false;
  for (const unsigned char *p = (const unsigned char *)id; *p; ++p)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '.' ||
          *p == '_' || *p == '-'))
      return false;
  return true;
}
static bool relative_path(const char *path) {
  if (!path || strlen(path) >= OUSE_APP_FILE_PATH)
    return false;
  if (!*path)
    return true;
  const unsigned char *segment = (const unsigned char *)path, *p = segment;
  for (;; ++p) {
    if (!*p || *p == '/') {
      size_t n = p - segment;
      if (!n || segment[0] == '.' || segment[n - 1] == '.' ||
          segment[n - 1] == ' ' || n > 80)
        return false;
      if ((n >= 5 && !strncasecmp((const char *)p - 5, ".part", 5)) ||
          (n >= 4 && !strncasecmp((const char *)p - 4, ".bak", 4)))
        return false;
      if (!*p)
        return true;
      segment = p + 1;
    } else if (*p < 32 || strchr("\\:<>\"|?*", *p))
      return false;
  }
}
bool ouse_app_files_path(const char *id, const char *relative, char *absolute,
                         size_t capacity) {
  if (!valid_app(id) || !relative_path(relative) || !absolute)
    return false;
  int n = snprintf(absolute, capacity, OUSE_APP_FILES_ROOT "/%s%s%s", id,
                   *relative ? "/" : "", relative);
  return n > 0 && (size_t)n < capacity;
}
static esp_err_t directory(const char *path) {
  if (mkdir(path, 0755) == 0)
    return ESP_OK;
  struct stat st;
  return errno == EEXIST && !stat(path, &st) && S_ISDIR(st.st_mode) ? ESP_OK
                                                                    : ESP_FAIL;
}
esp_err_t ouse_app_files_prepare(const char *id) {
  char path[256];
  if (!ouse_app_files_path(id, "", path, sizeof(path)))
    return ESP_ERR_INVALID_ARG;
  if (directory(OUSE_APP_FILES_ROOT) != ESP_OK)
    return ESP_FAIL;
  return directory(path);
}
static esp_err_t file_error(void) {
  return errno == ENOENT   ? ESP_ERR_NOT_FOUND
         : errno == ENOSPC ? ESP_ERR_NO_MEM
                           : ESP_FAIL;
}
typedef struct {
  char text[16384];
  size_t used;
  bool failed;
} json_t;
static void append(json_t *out, const char *format, ...) {
  if (out->failed)
    return;
  va_list args;
  va_start(args, format);
  int n = vsnprintf(out->text + out->used, sizeof(out->text) - out->used,
                    format, args);
  va_end(args);
  if (n < 0 || (size_t)n >= sizeof(out->text) - out->used)
    out->failed = true;
  else
    out->used += n;
}
static void string(json_t *out, const char *value) {
  append(out, "\"");
  for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
    if (*p == '\"' || *p == '\\')
      append(out, "\\%c", *p);
    else if (*p < 32)
      append(out, "\\u%04x", *p);
    else
      append(out, "%c", *p);
  }
  append(out, "\"");
}
static void entry(json_t *out, const char *name, const struct stat *st) {
  append(out, "{\"name\":");
  string(out, name);
  append(out, ",\"bytes\":%lld,\"directory\":%s}", (long long)st->st_size,
         S_ISDIR(st->st_mode) ? "true" : "false");
}
static bool stopped(void *context) {
  return ouse_task_cancel_requested(((file_job_t *)context)->id);
}
static esp_err_t sink(void *context, const unsigned char *data, size_t bytes) {
  file_job_t *job = context;
  if (stopped(job))
    return ESP_ERR_INVALID_STATE;
  if (job->downloaded + bytes > FILE_LIMIT)
    return ESP_ERR_INVALID_SIZE;
  if (fwrite(data, 1, bytes, job->stream) != bytes)
    return file_error();
  job->downloaded += bytes;
  return ESP_OK;
}
static esp_err_t execute(file_job_t *job, char **result) {
  if (!ouse_files_lease_begin())
    return ESP_ERR_INVALID_STATE;
  char path[320], temporary[344], other[600];
  json_t *out =
      heap_caps_calloc(1, sizeof(*out), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  FILE *file = NULL;
  esp_err_t error =
      out ? ouse_app_files_prepare(job->owner.app_id) : ESP_ERR_NO_MEM;
  if (error != ESP_OK)
    goto done;
  if (!ouse_app_files_path(job->owner.app_id, job->path, path, sizeof(path))) {
    error = ESP_ERR_INVALID_ARG;
    goto done;
  }
  struct stat st;
  snprintf(other, sizeof(other), "%s.bak", path);
  if (stat(path, &st) && errno == ENOENT && !stat(other, &st) &&
      rename(other, path)) {
    error = ESP_FAIL;
    goto done;
  }
  if ((job->operation == OUSE_APP_FILE_WRITE ||
       job->operation == OUSE_APP_FILE_WRITE_CHUNK ||
       job->operation == OUSE_APP_FILE_DOWNLOAD ||
       job->operation == OUSE_APP_FILE_READ) &&
      !stat(path, &st) && !S_ISREG(st.st_mode)) {
    error = ESP_ERR_INVALID_ARG;
    goto done;
  }
  switch (job->operation) {
  case OUSE_APP_FILE_LIST: {
    DIR *dir = opendir(path);
    if (!dir) {
      error = file_error();
      break;
    }
    append(out, "{\"entries\":[");
    unsigned seen = 0, count = 0;
    bool more = false;
    struct dirent *e;
    int64_t deadline = esp_timer_get_time() + 3000000;
    while ((e = readdir(dir))) {
      if (esp_timer_get_time() > deadline) {
        error = ESP_ERR_TIMEOUT;
        break;
      }
      if (stopped(job)) {
        error = ESP_ERR_INVALID_STATE;
        break;
      }
      size_t namelen = strlen(e->d_name);
      if (e->d_name[0] == '.' ||
          (namelen >= 5 && !strcasecmp(e->d_name + namelen - 5, ".part")) ||
          (namelen >= 4 && !strcasecmp(e->d_name + namelen - 4, ".bak")))
        continue;
      if (seen++ < job->offset)
        continue;
      if (count == 16) {
        more = true;
        break;
      }
      int n = snprintf(other, sizeof(other), "%s/%s", path, e->d_name);
      if (n <= 0 || (size_t)n >= sizeof(other) || stat(other, &st))
        continue;
      if (count)
        append(out, ",");
      entry(out, e->d_name, &st);
      ++count;
    }
    closedir(dir);
    append(out, "],\"more\":%s,\"nextOffset\":%u}", more ? "true" : "false",
           seen - (more ? 1 : 0));
    break;
  }
  case OUSE_APP_FILE_STAT:
    if (stat(path, &st))
      error = file_error();
    else
      entry(out, job->path, &st);
    break;
  case OUSE_APP_FILE_READ: {
    unsigned char *bytes = heap_caps_malloc(
        job->bytes ? job->bytes : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char *hex = heap_caps_malloc(job->bytes * 2 + 1,
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!bytes || !hex) {
      free(bytes);
      free(hex);
      error = ESP_ERR_NO_MEM;
      break;
    }
    file = fopen(path, "rb");
    if (!file) {
      error = file_error();
      free(bytes);
      free(hex);
      break;
    }
    if (fseek(file, job->offset, SEEK_SET)) {
      error = ESP_FAIL;
      free(bytes);
      free(hex);
      break;
    }
    size_t n = fread(bytes, 1, job->bytes, file);
    if (ferror(file))
      error = ESP_FAIL;
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i) {
      hex[2 * i] = digits[bytes[i] >> 4];
      hex[2 * i + 1] = digits[bytes[i] & 15];
    }
    hex[2 * n] = 0;
    string(out, hex);
    free(bytes);
    free(hex);
    break;
  }
  case OUSE_APP_FILE_WRITE:
  case OUSE_APP_FILE_WRITE_CHUNK:
  case OUSE_APP_FILE_DOWNLOAD: {
    bool atomic = job->operation != OUSE_APP_FILE_WRITE_CHUNK;
    snprintf(temporary, sizeof(temporary), "%s.part", path);
    if (atomic)
      file = fopen(temporary, "wb");
    else {
      file = fopen(path, "r+b");
      if (!file && errno == ENOENT)
        file = fopen(path, "w+b");
    }
    if (!file) {
      error = file_error();
      break;
    }
    if (job->operation == OUSE_APP_FILE_DOWNLOAD) {
      ouse_http_request_t request = {
          .url = job->data, .method = "GET", .timeout_ms = 30000};
      ouse_http_response_t *response = heap_caps_calloc(
          1, sizeof(*response), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!response) {
        error = ESP_ERR_NO_MEM;
        fclose(file);
        file = NULL;
        unlink(temporary);
        break;
      }
      job->stream = file;
      error = ouse_http_transfer(&request, esp_timer_get_time() + 30000000,
                                 FILE_LIMIT, response, sink, stopped, job);
      job->stream = NULL;
      if (error == ESP_OK &&
          (response->status < 200 || response->status >= 300))
        error = ESP_FAIL;
      free(response);
    } else if (fseek(file, job->offset, SEEK_SET) ||
               fwrite(job->data, 1, job->bytes, file) != job->bytes)
      error = file_error();
    if (error == ESP_OK && (fflush(file) || fsync(fileno(file))))
      error = ESP_FAIL;
    if (fclose(file) && error == ESP_OK)
      error = ESP_FAIL;
    file = NULL;
    if (stopped(job))
      error = ESP_ERR_INVALID_STATE;
    if (atomic) {
      bool backup = false;
      snprintf(other, sizeof(other), "%s.bak", path);
      if (error == ESP_OK && !stat(path, &st)) {
        unlink(other);
        if (rename(path, other))
          error = file_error();
        else
          backup = true;
      }
      if (error == ESP_OK && rename(temporary, path))
        error = file_error();
      if (error != ESP_OK) {
        unlink(temporary);
        if (backup)
          (void)rename(other, path);
      } else if (backup)
        unlink(other);
    }
    append(out, "%u",
           (unsigned)(job->operation == OUSE_APP_FILE_DOWNLOAD ? job->downloaded
                                                               : job->bytes));
    break;
  }
  case OUSE_APP_FILE_MKDIR:
    error = directory(path);
    break;
  case OUSE_APP_FILE_REMOVE:
    if (stat(path, &st))
      error = file_error();
    else if (S_ISDIR(st.st_mode) ? rmdir(path) : unlink(path))
      error = file_error();
    break;
  case OUSE_APP_FILE_RENAME:
    if (!ouse_app_files_path(job->owner.app_id, job->data, other,
                             sizeof(other)))
      error = ESP_ERR_INVALID_ARG;
    else if (!stat(other, &st))
      error = ESP_ERR_INVALID_STATE;
    else if (rename(path, other))
      error = file_error();
    break;
  }
done:
  if (file)
    fclose(file);
  if (error == ESP_OK) {
    if (!out->used)
      append(out, "true");
    if (out->failed)
      error = ESP_ERR_INVALID_SIZE;
    else {
      *result =
          heap_caps_malloc(out->used + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (*result)
        memcpy(*result, out->text, out->used + 1);
      else
        error = ESP_ERR_NO_MEM;
    }
  }
  free(out);
  ouse_files_lease_end();
  return error;
}
static void worker(void *unused) {
  (void)unused;
  file_job_t *job;
  for (;;)
    if (xQueueReceive(jobs, &job, portMAX_DELAY) == pdTRUE) {
      char *result = NULL;
      bool started = ouse_task_start(job->id, true);
      esp_err_t error = started ? execute(job, &result) : ESP_ERR_INVALID_STATE;
      bool cancelled = ouse_task_cancel_requested(job->id);
      if (cancelled && error == ESP_OK) {
        error = ESP_ERR_INVALID_STATE;
        free(result);
        result = NULL;
      }
      if (started)
        ouse_task_finish(job->id, error, cancelled);
      job->complete(job->context, job->id, error, result);
      free(job);
    }
}
esp_err_t ouse_app_files_init(void) {
  if (jobs)
    return ESP_OK;
  jobs = xQueueCreate(8, sizeof(file_job_t *));
  if (!jobs)
    return ESP_ERR_NO_MEM;
  if (xTaskCreateWithCaps(worker, "app_sd", 16384, NULL, 2, NULL,
                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    vQueueDelete(jobs);
    jobs = NULL;
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}
esp_err_t ouse_app_files_submit(const ouse_task_owner_t *owner,
                                ouse_app_file_operation_t operation,
                                const char *path, uint32_t offset,
                                const void *data, size_t bytes, uint32_t *id,
                                ouse_app_file_complete_t complete,
                                void *context) {
  char absolute[320];
  if (!owner || !owner->instance || !complete || !id ||
      operation > OUSE_APP_FILE_DOWNLOAD ||
      !ouse_app_files_path(owner->app_id, path, absolute, sizeof(absolute)) ||
      bytes > OUSE_APP_FILE_CHUNK || offset > FILE_LIMIT ||
      bytes > FILE_LIMIT - offset)
    return ESP_ERR_INVALID_ARG;
  if (!*path && operation != OUSE_APP_FILE_LIST &&
      operation != OUSE_APP_FILE_STAT)
    return ESP_ERR_INVALID_ARG;
  if ((operation == OUSE_APP_FILE_RENAME ||
       operation == OUSE_APP_FILE_DOWNLOAD) &&
      (!data || memchr(data, 0, bytes) || !bytes))
    return ESP_ERR_INVALID_ARG;

  if (operation == OUSE_APP_FILE_WRITE && offset)
    return ESP_ERR_INVALID_ARG;
  if (!jobs)
    return ESP_ERR_INVALID_STATE;
  file_job_t *job =
      heap_caps_calloc(1, sizeof(*job), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!job)
    return ESP_ERR_NO_MEM;
  job->owner = *owner;
  job->operation = operation;
  strcpy(job->path, path);
  job->offset = offset;
  job->bytes = bytes;
  job->complete = complete;
  job->context = context;
  if (data && bytes)
    memcpy(job->data, data, bytes);
  if (operation == OUSE_APP_FILE_RENAME &&
      (!relative_path(job->data) || !*job->data)) {
    free(job);
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t error = ouse_task_create(owner, OUSE_TASK_STORAGE, "sdFile",
                                     OUSE_TASK_STOP_ON_CLOSE, true, id);
  if (error == ESP_OK) {
    job->id = *id;
    ouse_task_hold_result(*id, true);
    if (xQueueSend(jobs, &job, 0) == pdTRUE)
      return ESP_OK;
    ouse_task_hold_result(*id, false);
    ouse_task_finish(*id, ESP_ERR_NO_MEM, false);
    error = ESP_ERR_NO_MEM;
  }
  free(job);
  return error;
}

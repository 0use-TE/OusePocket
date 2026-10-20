#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#define OUSE_JOURNAL_PAGE 4096
#define OUSE_JOURNAL_LIMIT (1024*1024)
typedef struct {bool ready,busy;unsigned revision,page,pages,dropped;size_t bytes;esp_err_t error,export_error;bool exporting;char exported[128];char text[OUSE_JOURNAL_PAGE+1];} ouse_journal_view_t;
esp_err_t ouse_journal_start(void);
void ouse_journal_request(unsigned page,bool clear);
esp_err_t ouse_journal_export(void);
void ouse_journal_get(ouse_journal_view_t *out);
bool ouse_journal_flush(unsigned timeout_ms);

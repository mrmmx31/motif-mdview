#ifndef MD_DOCUMENT_H
#define MD_DOCUMENT_H
#include <stddef.h>
#define MDV_VERSION "0.1.0-dev"
#define MDV_MAX_INPUT (8u * 1024u * 1024u)
#define MDV_MAX_SPANS 200000u
#define MDV_MAX_LINES 200000u
/* Deliberately small Markdown subset, not a CommonMark parser. */
enum MdStyle {
 MD_NORMAL = 0, MD_BOLD = 1u, MD_ITALIC = 2u, MD_CODE = 4u,
 MD_LINK = 8u, MD_QUOTE = 16u, MD_H1 = 32u, MD_H2 = 64u,
 MD_H3 = 128u, MD_RULE = 256u
};
typedef struct { size_t start, end; unsigned style; } MdSpan;
typedef struct {
 char *text;
 size_t length, capacity;
 MdSpan *spans;
 size_t span_count, span_capacity;
 size_t source_bytes, source_lines;
} MdDocument;
/* On failure, output is empty and error contains a readable explanation. */
int md_parse(const unsigned char *input, size_t length, MdDocument *out,
             char *error, size_t error_size);
int md_parse_raw(const unsigned char *input, size_t length, MdDocument *out,
                 char *error, size_t error_size);
int md_load_mode(const char *path, int raw, MdDocument *out, char *error, size_t error_size);
int md_load(const char *path, MdDocument *out, char *error, size_t error_size);
void md_free(MdDocument *doc);
#endif

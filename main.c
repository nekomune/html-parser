#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

typedef struct html_attr_t html_attr_t;
struct html_attr_t {
    char *name;
    char *value;

    html_attr_t *next;
};

typedef struct html_tag_t html_tag_t;
struct html_tag_t {
    char *name;
    char *content;

    html_attr_t *attr;

    html_tag_t *child;
    html_tag_t *next;
};

typedef void *(func_f)(FILE *);
typedef int (typefunc_f)(int);

void *
malloc_or_die(size_t n)
{
    void *z = malloc(n);
    if (!z) {
        perror("malloc");
        exit(1);
    }
    else
        return z;
}

void *
calloc_or_die(size_t n, size_t m)
{
    void *z = calloc(n, m);
    if (!z) {
        perror("calloc");
        exit(1);
    }
    else
        return z;
}

void *
realloc_or_die(void *p, size_t n)
{
    void *z = realloc(p, n);
    if (!z) {
        perror("realloc");
        exit(1);
    }
    else
        return z;
}

bool
parse_check(func_f *func, FILE *f)
{
    void *p;
    fpos_t start_pos;
    fgetpos(f, &start_pos);
    p = func(f);
    fsetpos(f, &start_pos);
    if (p) {
        free(p);
        return true;
    } else {
        return false;
    }
}

void *
parse_whitespace(FILE *f)
{
    long start = ftell(f);
    while (isspace(fgetc(f)));
    fseek(f, -1L, SEEK_CUR);
    if (ftell(f) - start > 0)
        return malloc_or_die(1);
    else
        return NULL;
}

bool
parse_char(FILE *f, int c)
{
    if (fgetc(f) == c)
        return true;
    else
        fseek(f, -1L, SEEK_CUR);
    return false;
}

char *
parse_stringif(typefunc_f is_a, FILE *f)
{
    long start = ftell(f);
    while(is_a(fgetc(f)));
    fseek(f, -1L, SEEK_CUR);
    size_t len = ftell(f) - start;
    if (len > 0) {
        char *c = malloc_or_die(len);
        fseek(f, start, SEEK_SET);
        fread(c, len, 1, f);
        return c;
    }
    else
        return NULL;
}

char *
parse_stringifnot(typefunc_f is_a, FILE *f)
{
    long start = ftell(f);
    while(!is_a(fgetc(f)));
    fseek(f, -1L, SEEK_CUR);
    size_t len = ftell(f) - start;
    if (len > 0) {
        char *c = malloc_or_die(len);
        fseek(f, start, SEEK_SET);
        fread(c, len, 1, f);
        return c;
    }
    else
        return NULL;
}

char *
parse_until(FILE *f, int c)
{
    long start = ftell(f);
    while(fgetc(f) != c);

    if (!feof(f)) {
        fseek(f, -1L, SEEK_CUR);
        size_t len = ftell(f) - start;
        if (len > 0) {
            char *c = malloc_or_die(len);
            fseek(f, start, SEEK_SET);
            fread(c, len, 1, f);
            return c;
        }
        else
            return NULL;
    }
}

char *
parse_until_s(FILE *f, const char *s)
{
    long start = ftell(f);
    char *buf = malloc_or_die(strlen(s));
    while (!feof(f)) {
        while(fgetc(f) != *s);
        fseek(f, -1L, SEEK_CUR);
        fread(buf, 1, strlen(s), f);
        if (strcmp(buf, s))
            continue;
        // else it's a match
        free(buf);
        size_t len = ftell(f) -strlen(s) - start;
        fseek(f, start, SEEK_SET);
        if (len > 0) {
            char *res = malloc_or_die(len);
            fread(res, len, 1, f);
            return res;
        }
        else
            return NULL;
    }
}

char *
parse_dquotestring(FILE *f)
{
    if (!parse_char(f, '"'))
        return NULL;
    char *c = parse_until(f, '"');
    fseek(f, 1L, SEEK_CUR);
    return c;
}

int
isattr(int c)
{
    return
        !isspace(c)
        && c != '"'
        && c != '\''
        && c != '>'
        && c != '/'
        && c != '=';
}

static int
vcontains(const char *s, const char **v, size_t len)
{
    for (size_t i = 0; i < len; i++)
        if (!strcmp(s, v[i]))
            return true;

    return false;
}


int
istagnormal(const char *s)
{
    return 1;
}

int
istagvoid(const char *s)
{
    const char *v[] = {
        "area", "base", "br", "col",
        "embed", "hr", "img", "input",
        "keygen", "link", "meta", "param",
        "source", "track", "wbr"
    };

    return vcontains(s, v, sizeof(v) / sizeof(*v));
}

int
istagraw(const char *s)
{
    const char *v[] = {
        "script", "style"
    };

    return vcontains(s, v, sizeof(v) / sizeof(*v));
}

int
istagescapable(const char *s)
{
    const char *v[] = {
        "textarea", "title"
    };

    return vcontains(s, v, sizeof(v) / sizeof(*v));
}

int
istag(int c)
{
    return isalnum(c);
}

html_attr_t *
parse_attributes(FILE *f)
{
    html_attr_t *attr = calloc_or_die(1, sizeof(*attr));
    fpos_t start_pos;
    fgetpos(f, &start_pos);
    parse_whitespace(f);
    attr->name = parse_stringif(isattr, f);
    if (!attr->name)
        goto fail;
    if (!parse_char(f, '='))
        goto fail;
    attr->value = parse_dquotestring(f);
    attr->next = parse_attributes(f);

success:
    return attr;
fail:
    fsetpos(f, &start_pos);
    free(attr);
    return NULL;
}

void
content_append(char **s, char *t)
{
    if (!t)
        return;
    if (!*s)
        *s = t;
    else {
        *s = realloc_or_die(*s, strlen(*s) + strlen(t) + 2);
        strcpy(*s, t);
        free(t);
    }
}

html_tag_t *
parse_tag(FILE *f)
{
    static int debug_level = 0;
    if (feof(f))
        return NULL;

    html_tag_t *tag = calloc_or_die(1, sizeof(*tag));
    fpos_t start_pos;
    const char *word;

    fgetpos(f, &start_pos);

    parse_whitespace(f);
    if (!parse_char(f, '<'))
        goto fail;
    tag->name = parse_stringif(istag, f);
    if (!tag->name)
        goto fail;
    tag->attr = parse_attributes(f);
    parse_whitespace(f);
    if (!parse_char(f, '>'))
        goto fail;

    //printf("%02d%*s%s\n", debug_level, debug_level + 1, "", tag->name);

    // Process content and children
    if (istagvoid(tag->name)) {
        // BUG here, the data discarded here should be appended to the content
        // of the parent, but I'm not yet sure how I can cleanly do that.
        // Hopefully the information lost here is minor.
        free(parse_until(f, '<'));
    }
    else if (istagraw(tag->name)) {
        content_append(&tag->content, parse_until_s(f, "</"));
        if (!parse_char(f, '<'))
            goto fail;
        if (!parse_char(f, '/'))
            goto fail;
        word = parse_stringif(istag, f);
        if (!word)
            goto fail;
        if (strcmp(tag->name, word))
            goto fail;
        if (!parse_char(f, '>'))
            goto fail;
    }
    else { // Regular HTML tag
        content_append(&tag->content, parse_until(f, '<'));
        debug_level++; 
        tag->child = parse_tag(f);
        debug_level--;
        if (tag->child)
            content_append(&tag->content, parse_until_s(f, "</"));
        if (!parse_char(f, '<'))
            goto fail;
        if (!parse_char(f, '/'))
            goto fail;
        word = parse_stringif(istag, f);
        if (!word)
            goto fail;
        if (strcmp(tag->name, word))
            goto fail;
        if (!parse_char(f, '>'))
            goto fail;
    }

    // Sibling
    tag->next = parse_tag(f);
success:
    return tag;
fail:
    fsetpos(f, &start_pos);
    free(tag);
    return NULL;
}

html_tag_t *
parse_start(FILE *f)
{
    html_tag_t *root;
    if (parse_check(parse_whitespace, f)) {
        parse_whitespace(f);
        return parse_start(f);
    }
    //else if (parse_check(parse_comment, f)) {
    //    parse_comment(f);
    //    return parse_start(f);
    //}
    return parse_tag(f);
}

void
test(html_tag_t *tag)
{
    if (tag->child)
        test(tag->child);
    if (tag->next)
        test(tag->next);

    for (html_attr_t *a = tag->attr; a; a = a->next)
        if (!strcmp(a->name, "class") && !strcmp(a->value, "id2"))
            if (tag->child)
                printf("%s\n", tag->child->content);

}

int
main(int argc, const char **argv)
{
    FILE *f = NULL;
    if (argc > 1) {
        f = fopen(argv[1], "r");
        if (!f)
            perror(argv[1]);
    }

    if (!f)
        f = stdin;

    // Main parse loop
    // break shit into lexemes
    // Is recursive
    // parse_start
    //parse_root
    //parse_tag
    //parse_comment
    //parse_whitespace

    html_tag_t *tag = parse_start(f);

    // all the canbe can be implmented like:
    //pos = getpos(f)
    //parse_thing(f)
    //set pos of f back
    //return if can be parsed or not
    // parse_thing will set position back if it fails anyway

    // Time for my work to pay off
    test(tag);

    return 0;
}

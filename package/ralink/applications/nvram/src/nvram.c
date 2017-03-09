/******************************************************************
 * $File:   nvram.c
 *
 * $Author: Hua Shao
 * $Date:   Oct, 2014
 *
 ******************************************************************/

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "nvram.h"
#include "crc32.h"
#include "flash.h"


#define MAX_ENTRY 500


typedef struct {
    char * name;
    char * value;
} entry_t;

typedef struct {
    char * name;
    uint32_t offset;
    uint32_t size;
    uint32_t flag;
    uint32_t crc;
    entry_t cache[MAX_ENTRY];
} section_t;


#define NV_RO  (1)      /* read only */
#define NV_IV  (1<<1)   /* invisible */

typedef struct {
    char * name;
    uint32_t offset;
    uint32_t size;
    uint32_t flag;
} layout_t;

#include "layout.h"

#ifndef NVRAM_CUSTOM_LAYOUT

/* this is a demo layout,
   please make your own layout in layout.h
*/

static layout_t nvlayout[] =
{
    /* the first 4KB of "Config" is reserved for uboot. */
    {
        .name = "uboot",
        .offset = 0x0,
        .size = 0x1000,
        .flag = NV_RO,
    },
    {
        .name = "hidden",
        .offset = 0x1000,
        .size = 0x1000,
        .flag = NV_IV | NV_RO,
    },
    {
        .name = "test",
        .offset = 0x2000,
        .size = 0x4000,
        .flag = 0,
    },
    {
        .name = "test2",
        .offset = 0x6000,
        .size = 0x2000,
        .flag = NV_RO,
    },
    {
        .name = "test3",
        .offset = 0x8000,
        .size = 0x8000,
    },
};
#endif


static uint32_t nvram_mtd_size = 0;
static section_t * nvramcache = NULL;
static uint32_t sectionnum = sizeof(nvlayout)/sizeof(layout_t);


void dump(uint8_t     *p,
          uint32_t   s,
          uint8_t     *base)
{
    int32_t i, c;
    while ((int)s > 0)
    {
        if (base)
        {
            printf("ox%p: ", base);
        }
        else
        {
            printf("0x%p: ", p);
        }
        for (i = 0;  i < 16;  i++)
        {
            if (i < (int)s)
            {
                printf("%02X ", p[i] & 0xFF);
            }
            else
            {
                printf("   ");
            }
            if (i == 7) printf(" ");
        }
        printf(" |");
        for (i = 0;  i < 16;  i++)
        {
            if (i < (int)s)
            {
                c = p[i] & 0xFF;
                if ((c < 0x20) || (c >= 0x7F)) c = '.';
            }
            else
            {
                c = ' ';
            }
            printf("%c", c);
        }
        printf("|\n");
        s -= 16;
        p += 16;
    }
}



static section_t * _nvram_section(char * name)
{
    int32_t i = 0;
    for (i=0; i<sectionnum; i++)
    {
        if (0 == strcmp(nvramcache[i].name, name))
        {
            if( !(nvramcache[i].flag & NV_IV))
                return &nvramcache[i];
            else
                printf("* ");
        }
    }

    printf("no section named \"%s\"\n", name);
    return NULL;
}


static int32_t _nvram_buf_del(char * section, char * key)
{
    int32_t i = 0;
    section_t * sect = NULL;

    DEBUG("%s(%s, %s)\n", __FUNCTION__, section, key);

    sect = _nvram_section(section);
    if (!sect)
        return NG;

    if (sect->flag & NV_RO)
    {
        DEBUG("section \"%s\"is read-only!\n", section);
        return NG;
    }

    /* check if name exists */
    for (i=0; i<MAX_ENTRY; i++)
    {
        if (sect->cache[i].name &&
            0 == strcmp(key, sect->cache[i].name))
            break;
    }

    if (i >= MAX_ENTRY)
    {
        printf("no such entry!\n");
        return NG;
    }

    sfree(sect->cache[i].name);
    sfree(sect->cache[i].value);

    return OK;

}


static int32_t _nvram_buf_set(char * section, char * key, char * value)
{
    int32_t i = 0;
    section_t * sect = NULL;

    DEBUG("%s(%s,%s,%s)\n", __FUNCTION__, section, key, value);

    sect = _nvram_section(section);
    if (!sect)
        return NG;

    if (sect->flag & NV_RO)
    {
        DEBUG("section \"%s\"is read-only!\n", section);
        return NG;
    }

    /* check if name exists */
    for (i=0; i<MAX_ENTRY; i++)
    {
        if (sect->cache[i].name &&
            0 == strcmp(key, sect->cache[i].name))
            break;
    }
    if (i < MAX_ENTRY)
        goto __set;

    /* else, find an empty room */
    for (i=0; i<MAX_ENTRY; i++)
    {
        if (!sect->cache[i].name)
            break;
    }
    if (i >= MAX_ENTRY)
    {
        DEBUG("no empty room found! %s\n", __FUNCTION__);
        return NG;
    }

    ASSERT(!sect->cache[i].name);
    ASSERT(!sect->cache[i].value);

__set:
    sect->cache[i].name = strdup(key);
    sect->cache[i].value = strdup(value);

    return OK;
}

static char * _nvram_buf_get(char * section, char * key)
{
    int32_t i = 0;
    section_t * sect = NULL;

    DEBUG("%s(%s,%s)\n", __FUNCTION__, section, key);

    sect = _nvram_section(section);
    if (!sect)
        return NULL;

    for (i=0; i<MAX_ENTRY; i++)
    {
        if (sect->cache[i].name &&
            0 == strcmp(sect->cache[i].name, key))
        {
            printf("%s\n", sect->cache[i].value);
            return sect->cache[i].value;
        }
    }

    printf("no such entry!\n");
    return NULL;
}



int32_t nvram_parse(char * section, char * cachefile)
{
    int32_t rc = 0;
    FILE * fp = NULL;
    size_t len = 0;
    char * data, * p, * q;
    int32_t i = 0, j = 0;

    API();

    fp = fopen(cachefile, "r+b");
    if (!fp)
    {
        DEBUG("failed to load %s, %s, %s\n", cachefile, strerror(errno), __FUNCTION__);
        return NG;
    }

    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    data = (char *)malloc(len);
    if(!data)
    {
        DEBUG("failed to alloc buffer (%zu) for nvram! %s.\n", len, __FUNCTION__);
        fclose(fp);
        return NG;
    }
    memset(data, 0, len);
    rc = fread(data, 1, len, fp);
    DEBUG("rc %d, len %zu\n", rc, len);
    ASSERT(rc == len);

    nvramcache = (section_t *)malloc(sizeof(section_t)*sectionnum);
    memset(nvramcache, 0, sizeof(sizeof(section_t)*sectionnum));
    for (i = 0; i<sectionnum; i++)
    {
        uint32_t secrc = 0;
        nvramcache[i].name = nvlayout[i].name;
        nvramcache[i].flag = nvlayout[i].flag;
        nvramcache[i].size = nvlayout[i].size;
        nvramcache[i].offset = nvlayout[i].offset;
        j = 0;
        p = data+nvramcache[i].offset;
        secrc = *(uint32_t *)p;
        nvramcache[i].crc =
            crc32(0, (uint8_t *)(data + nvramcache[i].offset+4), nvramcache[i].size-4);

        // DEBUG("file crc: %x, cal crc: %x\n", secrc, nvramcache[i].crc);

        if (secrc == 0 || secrc != nvramcache[i].crc)
        {
            DEBUG("data error in section %s.\n", nvramcache[i].name);
            continue;
        }

        p = p + 4;
        /* parsing the content, k=v\0k1=v1\0......kx=vx */
        while (p < data+nvramcache[i].offset+nvramcache[i].size && j < MAX_ENTRY)
        {
            if (*p == 0) break;
            q = strchr(p, '=');
            if (!q)
            {
                DEBUG("nvram format error!\n");
                break;
            }
            *q = 0;
            q++;
            // DEBUG("\t<%s>=<%s>\n", p, q);
            nvramcache[i].cache[j].name = strdup(p);
            nvramcache[i].cache[j].value = strdup(q);
            p = q + strlen(q) + 1;
            j++;
        }
    }

    fclose(fp);
    return OK;
}


int32_t nvram_commit(char flash)
{
    int32_t rc = OK;
    int32_t sect = 0, i = 0;
    FILE * fp = NULL;
    char * buffer, * p, * q;

    API();

    fp = fopen(NVRAM_CACHE_FILE, "r+");
    if(!fp)
    {
        DEBUG("failed to open %s, %s.\n", NVRAM_CACHE_FILE, __FUNCTION__);
        rc = NG;
        goto __quit;
    }

    buffer = (char *)malloc(nvram_mtd_size);
    if(!buffer)
    {
        DEBUG("failed to alloc buffer (%d) for nvram! %s.\n", nvram_mtd_size, __FUNCTION__);
        rc = NG;
        goto __quit;
    }

    for (sect=0; sect<sectionnum; sect++)
    {
        p = buffer + nvramcache[sect].offset + 4; /* reserve 4 bytes for crc32 */
        q = p + nvramcache[sect].size;
        for (i=0; i<MAX_ENTRY; i++)
        {
            if (nvramcache[sect].cache[i].name)
            {
                int32_t l;
                ASSERT(nvramcache[sect].cache[i].value);
                l = strlen(nvramcache[sect].cache[i].name)
                  + strlen(nvramcache[sect].cache[i].value) + 2;
                if (p+l>=q)
                {
                    DEBUG("exceed nvram section size %u!\n", nvramcache[sect].size);
                    rc = NG;
                    goto __quit;
                }
                sprintf(p, "%s=%s",
                    nvramcache[sect].cache[i].name,
                    nvramcache[sect].cache[i].value);
                p += l;
            }
        }
        *p = '\0'; // just make sure

        nvramcache[sect].crc =
            crc32(0, (uint8_t *)(buffer + nvramcache[sect].offset+4), nvramcache[sect].size-4);
        *(uint32_t *)(buffer + nvramcache[sect].offset) = nvramcache[sect].crc;
        DEBUG("cal crc32 = 0x%8x!\n", nvramcache[sect].crc);
        p = buffer + nvramcache[sect].offset;

        fseek(fp, nvramcache[sect].offset, SEEK_SET);
        DEBUG("write nvram raw data, offset %u, size %u!\n",
            nvramcache[sect].offset, nvramcache[sect].size);
        i = fwrite(buffer + nvramcache[sect].offset, 1, nvramcache[sect].size, fp);
        ASSERT(i == nvramcache[sect].size);
    }

    if (flash)
    {
        i = flash_write(buffer, 0, nvram_mtd_size);
        ASSERT(i>0);
    }


__quit:
    sfree(buffer);
    fclose(fp);
    return rc;
}


char * nvram_get(char * section, char * key)
{
    API();
    return _nvram_buf_get(section, key);
}


int32_t nvram_del(char * section, char * key)
{
    API();

    if (OK == _nvram_buf_del(section, key))
        return nvram_commit(0);
    return NG;
}


int32_t nvram_set(char * section, char * key, char * value)
{
    API();
    if (OK == _nvram_buf_set(section, key, value))
        return nvram_commit(0);
    return NG;
}

int32_t nvram_show(char * section)
{
    int32_t i = 0, j = 0, counter = 0;
    API();
    for (i=0; i<sectionnum; i++)
    {
        if (nvramcache[i].flag & NV_IV) // skip invisible section
            continue;
        if (section && 0 != strcmp(section, nvramcache[i].name))
            continue;

        printf("\n\n++++++++++++ %s ++++++++++++ 0x%x\n", nvramcache[i].name, nvramcache[i].flag);
        counter = 0;
        for (j=0; j<MAX_ENTRY; j++)
        {
            if (nvramcache[i].cache[j].name)
            {
                counter++;
                printf("%s=%s\n",nvramcache[i].cache[j].name, nvramcache[i].cache[j].value);
            }
        }
        printf("++++++++++++ %s ++++++++++++ %d\n", nvramcache[i].name, counter);
    }
    return OK;
}




int32_t nvram_clear(char * section)
{
    int32_t i = 0;
    FILE * fp = NULL;
    section_t * sec = NULL;
    char * buffer = NULL;

    API();

    fp = fopen(NVRAM_CACHE_FILE, "r+b");
    if (!fp)
    {
        DEBUG("failed to create %s, %s, %s\n", NVRAM_CACHE_FILE, strerror(errno), __FUNCTION__);
        return NG;
    }

    sec = _nvram_section(section);
    if(!sec || (sec->flag & NV_RO))
    {
        DEBUG("invalid section!\n");
        return NG;
    }

    fseek(fp, sec->offset, SEEK_SET);

    buffer = (char *)malloc(sec->size);
    if(!buffer)
    {
        DEBUG("failed to alloc buffer (%d) for nvram! %s.\n", sec->size, __FUNCTION__);
        fclose(fp);
        return NG;
    }
    memset(buffer, 0, sec->size);

    sec->crc =
        crc32(0, (uint8_t *)(buffer+4), sec->size-4);
    *(uint32_t *)(buffer) = sec->crc;

    i = fwrite(buffer, sec->size, 1, fp);
    ASSERT(i == 1);

    fclose(fp);
    return OK;
}

int32_t nvram_reset(char * section, char * defile)
{
    API();

    if (OK == nvram_parse(section, defile))
        return nvram_commit(1);

    return NG;
}


int32_t nvram_filecache(char * defile)
{
    int32_t rc = OK;
    FILE * fp = NULL;
    struct stat statbuf;
    char * buffer = NULL;

    API();

    /* create a cache file */
    rc = stat(defile, &statbuf);
    if (!rc)
    {
        DEBUG("cache file %s is ready.\n", defile);
        return OK;
    }


    fp = fopen(defile, "w+b");
    if (!fp)
    {
        DEBUG("failed to create %s, %s, %s\n",
            defile, strerror(errno), __FUNCTION__);
        return NG;
    }
    DEBUG("cache file %s, created.\n", defile);

    buffer = (char *)malloc(nvram_mtd_size);
    if(!buffer)
    {
        DEBUG("failed to alloc %d bytes for , %s, %s\n",
            nvram_mtd_size, defile, __FUNCTION__);
        fclose(fp);
        return NG;
    }

    /* dump "Config" partition into a cache file. */
    memset(buffer, 0, nvram_mtd_size);

    DEBUG("read original data from flash.\n");
    if (flash_read(buffer,0,nvram_mtd_size) < 0)
    {
        fclose(fp);
        return NG;
    }

    //dump(buffer, nvram_mtd_size, 0);

    DEBUG("write into cache file.\n");
    rc = fwrite(buffer, 1, nvram_mtd_size, fp);
    if (rc != nvram_mtd_size)
    {
        DEBUG("failed to write data into %s, %s, %s\n",
            defile, strerror(errno), __FUNCTION__);
        return NG;
    }
    fclose(fp);
    return OK;
}



int32_t usage(int32_t reason)
{
    if (reason != OK)
        printf("Invalid usage! ");
    printf("Usage:\n"
        "\tnvram get <section> <name>\n"
        "\tnvram set <section> <name> [value]\n"
        "\tnvram commit\n"
        "\tnvram show [section]\n"
        "\tnvram clear <section>\n");
    return OK;
}


int32_t main(int32_t argc, char ** argv)
{
    int32_t i = 0;

    for(i=0; i<argc; i++)
        DEBUG("argv[%d]:%s\n", i, argv[i]);


    if (argc < 2)
        return usage(OK);

    nvram_mtd_size = mtd_size(NVRAM_MTD_NAME);
    DEBUG("nvram_mtd_size = %u\n", nvram_mtd_size);
    if (nvram_mtd_size <= 0)
        return NG;

    if (nvram_filecache(NVRAM_CACHE_FILE) != OK)
        return NG;

    if (nvram_parse(NULL, NVRAM_CACHE_FILE) != OK)
        return NG;

    if (0 == strcasecmp(argv[1], "commit"))
    {
        return nvram_commit(1);
    }
    else if (0 == strcasecmp(argv[1], "set"))
    {
        if (argc == 5)
            return nvram_set(argv[2], argv[3], argv[4]);
        else
            return usage(NG);
    }
    else if (0 == strcasecmp(argv[1], "get"))
    {
        if (argc < 4)
            return usage(NG);
        else
            nvram_get(argv[2], argv[3]);
    }
    else if (0 == strcasecmp(argv[1], "del"))
    {
        if (argc < 4)
            return usage(NG);
        else
            nvram_del(argv[2], argv[3]);
    }
    else if (0 == strcasecmp(argv[1], "show"))
    {
        if (argc < 3)
            return nvram_show(NULL);
        else
            return nvram_show(argv[2]);
    }
    else if (0 == strcasecmp(argv[1], "clear"))
    {
        return nvram_clear(argv[2]);
    }
    else if (0 == strcasecmp(argv[1], "reset"))
    {
        if (argc < 4)
            return usage(NG);
        else
            return nvram_reset(argv[2], argv[3]);
    }
    else
        return usage(NG);

    return OK;
}


